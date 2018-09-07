
/*
The following specially formatted comments tell the LBARD build environment about this radio.
See radio_type for the meaning of each field.
See radios.h target in Makefile to see how this comment is used to register support for the radio.

RADIO TYPE: RFD900,"rfd900","RFDesign RFD900, RFD868 or compatible",rfd900_radio_detect,rfd900_serviceloop,rfd900_receive_bytes,rfd900_send_packet,always_ready,0

*/



#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <time.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <assert.h>

#include "sync.h"
#include "lbard.h"
#include "radios.h"

int always_ready(void)
{
  return 1;
}

/*
  RFD900 has 255 byte maximum frames, but some bytes get taken in overhead.
  We then Reed-Solomon the body we supply, which consumes a further 32 bytes.
  This leaves a practical limit of somewhere around 200 bytes.
  Fortunately, they are 8-bit bytes, so we can get quite a bit of information
  in a single frame. 
  We have to keep to single frames, because we will have a number of radios
  potentially transmitting in rapid succession, without a robust collision
  avoidance system.
 
*/

// About one message per second on RFD900
// We add random()%250 ms to this, so we deduct half of that from the base
// interval, so that on average we obtain one message per second.
// 128K air speed / 230K serial speed means that we can in principle send
// about 128K / 256 = 512 packets per second. However, the FTDI serial USB
// drivers for Mac crash well before that point.
int message_update_interval=INITIAL_AVG_PACKET_TX_INTERVAL-(INITIAL_PACKET_TX_INTERVAL_RANDOMNESS/2);  // ms
int message_update_interval_randomness=INITIAL_PACKET_TX_INTERVAL_RANDOMNESS;
long long last_message_update_time=0;
long long next_message_update_time=0;
long long congestion_update_time=0;

#define MAX_PACKET_SIZE 255

// This need only be the maximum control header size + maximum packet size
#define RADIO_RXBUFFER_SIZE 64+MAX_PACKET_SIZE
unsigned char radio_rx_buffer[RADIO_RXBUFFER_SIZE];

int last_rx_rssi=-1;
unsigned char *packet_data=NULL;


#include "fec-3.0.1/fixed.h"
void encode_rs_8(data_t *data, data_t *parity,int pad);
int decode_rs_8(data_t *data, int *eras_pos, int no_eras, int pad);
#define FEC_LENGTH 32
#define FEC_MAX_BYTES 223

extern unsigned char my_sid[32];
extern char *my_sid_hex;
extern char *servald_server;
extern char *credential;
extern char *prefix;

int target_transmissions_per_4seconds=TARGET_TRANSMISSIONS_PER_4SECONDS;

int rfd900_serviceloop(int serialfd)
{
  // Deal with clocks running backwards sometimes
  if ((congestion_update_time-gettime_ms())>4000)
    congestion_update_time=gettime_ms()+4000;
  
  if (gettime_ms()>congestion_update_time) {
    /* Very 4 seconds count how many radio packets we have seen, so that we can
       dynamically adjust our packet rate based on our best estimate of the channel
       utilisation.  In other words, if there are only two devices on channel, we
       should be able to send packets very often. But if there are lots of stations
       on channel, then we should back-off.
    */

    double ratio = 1.00;
    if (target_transmissions_per_4seconds)
      ratio = (radio_transmissions_seen+radio_transmissions_byus)
	*1.0/target_transmissions_per_4seconds;
    else {
      fprintf(stderr,"WARNING: target_transmissions_per_4seconds = 0\n");
    }
    // printf("--- Congestion ratio = %.3f\n",ratio);
    if (ratio<0.95) {
      // Speed up: If we are way too slow, then double our rate
      // If not too slow, then just trim 10ms from our interval
      if (ratio<0.25) message_update_interval/=2;
      else {
	int adjust=10;
	if ((ratio<0.80)&&(message_update_interval>300)) adjust=20;
	if ((ratio<0.50)&&(message_update_interval>300)) adjust=50;
	if (ratio>0.90) adjust=3;
	// Only increase our packet rate, if we are not already hogging the channel
	// i.e., we are allowed to send at most 1/n of the packets.
	float max_packets_per_second=1;
	int active_peers=active_peer_count();
	if (active_peers) {
	  max_packets_per_second=(TARGET_TRANSMISSIONS_PER_4SECONDS/active_peers)
	    /4.0;
	}
	int minimum_interval=1000.0/max_packets_per_second;
	if (radio_transmissions_byus<=radio_transmissions_seen)
	  message_update_interval-=adjust;
	if (message_update_interval<minimum_interval)
	  message_update_interval=minimum_interval;
      }
    } else if (ratio>1.0) {
      // Slow down!  We slow down quickly, so as to try to avoid causing
      // too many colissions.
      message_update_interval*=(ratio+0.4);
      if (!message_update_interval) message_update_interval=50;
      if (message_update_interval>4000) message_update_interval=4000;
    }
    
    if (!radio_transmissions_seen) {
      // If we haven't seen anyone else transmit anything, then only transmit
      // at a slow rate, so that we don't jam the channel and flatten our battery
      // while waiting for a peer
      message_update_interval=1000;
    }
    
    // Make randomness 1/4 of interval, or 25ms, whichever is greater.
    // The addition of the randomness means that we should never actually reach
    // our target capacity.
    message_update_interval_randomness = message_update_interval >> 2;
    if (message_update_interval_randomness<25)
      message_update_interval_randomness=25;
    
    // Force message interval to be at least 150ms + randomness
    // This keeps duty cycle < about 10% always.
    // 4 - 5 packets per second is therefore the fastest that we will go
    // (256 byte packet @ 128kbit/sec takes ~20ms)
    if (message_update_interval<150)
      message_update_interval=150;
    
    printf("*** TXing every %d+1d%dms, ratio=%.3f (%d+%d)\n",
	   message_update_interval,message_update_interval_randomness,ratio,
	   radio_transmissions_seen,radio_transmissions_byus);
    congestion_update_time=gettime_ms()+4000;
    
    if (radio_transmissions_seen) {
      radio_silence_count=0;
    } else {
      radio_silence_count++;
      if (radio_silence_count>3) {
	// Radio silence for 4x4sec = 16 sec.
	// This might be due to a bug with the UHF radios where they just stop
	// receiving packets from other radios. Or it could just be that there is
	// no one to talk to. Anyway, resetting the radio is cheap, and fast, so
	// it is best to play it safe and just reset the radio.
	write_all(serialfd,"!Z",2);
	radio_silence_count=0;
      }
    }
    
    radio_transmissions_seen=0;
    radio_transmissions_byus=0;
  }
  
  return 0;
}

int rfd900_receive_bytes(unsigned char *bytes,int count)
{
  int i;
  for(i=0;i<count;i++) {
      
    bcopy(&radio_rx_buffer[1],&radio_rx_buffer[0],RADIO_RXBUFFER_SIZE-1);
    radio_rx_buffer[RADIO_RXBUFFER_SIZE-1]=bytes[i];

    /*
      The revised RFD900+ firmware for the Mesh Extender 2.0 sends a little
      more useful information.

      It is framed with UTF-8 characters for in and out trays for human readability,
      and the various internal fields have also been improved for human readability.

      Preamble - 4 bytes 0xf0, 0x9f, 0x93, 0xa5
      Radio temperature - 4 byts  <+/->nnn
      Degree UTF-8 symbol - 2 bytes 0xc2, 0xb0
      GPIO state - 6 bytes, each one of 0,1,X or x
      Board frequency band - 2 bytes, e.g., "86" or "91"
      Postamble - 4 bytes 0xf0, 0x9f, 0x93, 0xa4
    */
#define REPORT_LENGTH (4+4+2+6+2+4)
    int template[REPORT_LENGTH]={
      0xf0, 0x9f, 0x93, 0xa5,
      -1,-1,-1,-1,
      0xc2,0xb0,
      -1,-1,-1,-1,-1,-1,
      -1,-1,
      0xf0,0x9f,0x93,0xa4};
    int isReport=1;
    for(int i=0;i<REPORT_LENGTH;i++)
      if (template[i]!=-1)
	if (radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+i]!=template[i])
	  { isReport=0;
	    break; }
    if (isReport) {
      char tempstring[5]={radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+0],
			  radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+1],
			  radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+2],
			  radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+3],
			  0};
      radio_last_heartbeat_time=gettime_ms();
      radio_temperature=atoi(tempstring);
      printf("Radio temperature = %dC, frequency band = %c%c\n",
	     radio_temperature,
	     radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+4+2+6+0],
	     radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+4+2+6+1]);
      if (debug_gpio) {
	printf("GPIO ADC values = [");
	for(int j=0;j<6;j++) {
	  printf("%c",
		 radio_rx_buffer[RADIO_RXBUFFER_SIZE-REPORT_LENGTH+4+4+2+j]);
	}
	printf("]  Radio TX interval = %dms, TX seen = %d, TX us = %d\n",
	       message_update_interval,
	       radio_transmissions_seen,
	       radio_transmissions_byus);
      }
      
    } else if ((radio_rx_buffer[RADIO_RXBUFFER_SIZE-1]==0xdd)
	       &&(radio_rx_buffer[RADIO_RXBUFFER_SIZE-8]==0xec)
	       &&(radio_rx_buffer[RADIO_RXBUFFER_SIZE-9]==0xce))
      // Support old-style RFD900 Mesh Extender firmware reports
      {
	if (debug_gpio) {
	  printf("GPIO ADC values = ");
	  for(int j=0;j<6;j++) {
	    printf("%s0x%02x",
		   j?",":"",
		   radio_rx_buffer[RADIO_RXBUFFER_SIZE-7+j]);
	  }
	  printf(".  Radio TX interval = %dms, TX seen = %d, TX us = %d\n",
		 message_update_interval,
		 radio_transmissions_seen,
		 radio_transmissions_byus);
	}
      } else if ((radio_rx_buffer[RADIO_RXBUFFER_SIZE-1]==0x55)
		 &&(radio_rx_buffer[RADIO_RXBUFFER_SIZE-8]==0x55)
		 &&(radio_rx_buffer[RADIO_RXBUFFER_SIZE-9]==0xaa))
      {
	// Found RFD900 CSMA envelope: packet was immediately before this
	int packet_bytes=radio_rx_buffer[RADIO_RXBUFFER_SIZE-4];
	radio_last_heartbeat_time=gettime_ms();
	radio_temperature=radio_rx_buffer[RADIO_RXBUFFER_SIZE-5];
	last_rx_rssi=radio_rx_buffer[RADIO_RXBUFFER_SIZE-7];
	
	int buffer_space=radio_rx_buffer[RADIO_RXBUFFER_SIZE-3];
	buffer_space+=radio_rx_buffer[RADIO_RXBUFFER_SIZE-2]*256;	

	if (packet_bytes>MAX_PACKET_SIZE) packet_bytes=0;       
	packet_data = &radio_rx_buffer[RADIO_RXBUFFER_SIZE-9-packet_bytes];
	radio_transmissions_seen++;
	
	if (packet_bytes) {
	  // Have whole packet
	  if (debug_radio)
	    message_buffer_length+=
	      snprintf(&message_buffer[message_buffer_length],
		       message_buffer_size-message_buffer_length,
		       "Saw RFD900 CSMA Data frame: temp=%dC, last rx RSSI=%d, frame len=%d\n",
		       radio_temperature, last_rx_rssi,
		       packet_bytes);

	  if (saw_packet(packet_data,packet_bytes,last_rx_rssi,
			 my_sid_hex,prefix,
			 servald_server,credential)) {
	  } else {
	  }
	  	  
	  packet_bytes=0;
	}
      }
  }
  return 0;
}

int rfd900_radio_detect(int fd)
{
  /* Initialise an RFD900 radio.
     Here we want to reset the radio, and read out its attached I2C EEPROM, if any,
     and set our max TX rate etc based on what we read from there, so that we can
     adhere to the encoded radio regulations.

     XXX - Allow txfreq and txpower options to override the stored defaults for now.
  */

  // We require a serial port
  if (fd==-1) return -1;
  
  fprintf(stderr,"WARNING: Assuming RFD900 radio without probing.\n");

  serial_setup_port_with_speed(fd,230400);

  radio_set_type(RADIOTYPE_RFD900);
  
  eeprom_read(fd);

  return 1;
}

/* 
   On the MR3020 hardware, there is a 3-position slider switch.
   If the switch is in the centre position, then we enable high
   power output.

   WARNING: You will need a spectrum license to be able to operate
   in high-power mode, because the CSMA RFD900a firmware is normally
   only legal at an EIRP of 3dBm or less, which allowing for a +3dB
   antanna means that 1dBm is the highest TX power that is realistically
   safe.
 
   Because of the above, we require that /dos/hipower.en exist on the file
   system as well as the switch being in the correct position.  The Mesh
   Extender default image does not include the /dos/hipower.en file, and it
   must be added manually to enable hi-power mode.
 */
unsigned char hipower_en=0;
unsigned char hipower_switch_set=0;
long long hi_power_timeout=3000; // 3 seconds between checks
long long next_check_time=0;
int rfd900_set_tx_power(int serialfd)
{
  char *safety_file="/dos/lowpower";
#if 0
  char *gpio_file="/sys/kernel/debug/gpio";
  char *gpio_string=" gpio-18  (sw1                 ) in  lo";
#endif

  if (next_check_time<gettime_ms()) {
    hipower_en=1;
    hipower_switch_set=0;
    next_check_time=gettime_ms()+hi_power_timeout;

    FILE *f=fopen(safety_file,"r");
    if (f) {
      hipower_en=0;
      fclose(f);
    }
#if 1
    hipower_switch_set=1;
#else
    f=fopen(gpio_file,"r");
    if (f) {
      char line[1024];
      line[0]=0; fgets(line,2014,f);
      while(line[0]) {
	if (!strncmp(gpio_string,line,strlen(gpio_string)))
	  hipower_switch_set=1;
	
	line[0]=0; fgets(line,2014,f);
      }
      fclose(f);
    }
#endif
  }

  if ((hipower_switch_set&&hipower_en&&(txpower==-1))||txpower==24) {
    // printf("Setting radio to hipower\n");
    if (write_all(serialfd,"!H",3)==-1) serial_errors++; else serial_errors=0;
  } else if (txpower==30) {
    printf("Setting radio to maximum power (30dBm)\n");
    if (write_all(serialfd,"!M",3)==-1) serial_errors++; else serial_errors=0;
  } else if (txpower!=1&&txpower!=-1) {
    fprintf(stderr,"Unsupported TX power level selected: use 1, 24 or 30 dBm for RFD900/RFD868 radios.\n");
    exit(-1);
  }  else {
    printf("Setting radio to lowpower mode (flags %d:%d) -- probably ok under Australian LIPD class license, but you should check.\n",
	   hipower_switch_set,hipower_en);
    if (write_all(serialfd,"!L",3)==-1) serial_errors++; else serial_errors=0;
  }

  // Also allow setting of TX frequency
  if (txfreq!=-1) {
    char cmd[16];
    snprintf(cmd,16,"%x!f",txfreq/1000);
    if (write_all(serialfd,cmd,strlen(cmd))==-1) serial_errors++; else serial_errors=0;    
  }

  return 0;
}

int rfd900_send_packet(int serialfd,unsigned char *out, int offset)
{
  // Now escape any ! characters, and append !! to the end for the RFD900 CSMA
  // packetised firmware.

  unsigned char escaped[2+offset*2+2];
  int elen=0;
  int i;

  rfd900_set_tx_power(serialfd);

  // Sometimes the ! gets eaten here. Solution is to
  // send a non-! character first, so that even if !-mode
  // is set, all works properly.  this will also stop us
  // accidentally doing !!, which will send a packet.
  write(serialfd,"C!C",3);
  // Then stuff the escaped bytes to send
  for(i=0;i<offset;i++) {
    if (out[i]=='!') {
      escaped[elen++]='!'; escaped[elen++]='.';
    } else escaped[elen++]=out[i];
  }
  // Finally include TX packet command
  escaped[elen++]='!'; escaped[elen++]='!';
  if (debug_radio_tx) {
    dump_bytes(stdout,"sending packet",escaped,elen);    
  }  

  if (write_all(serialfd,escaped,elen)==-1) {
    serial_errors++;
    return -1;
  } else {
    serial_errors=0;
    return 0;
  }
}
