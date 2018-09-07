
/*
The following specially formatted comments tell the LBARD build environment about this radio.
See radio_type for the meaning of each field.
See radios.h target in Makefile to see how this comment is used to register support for the radio.

RADIO TYPE: HFCODAN,"hfcodan","Codan HF with ALE",hfcodanbarrett_radio_detect,hfcodan_serviceloop,hfcodan_receive_bytes,hfcodan_send_packet,hf_radio_check_if_ready,10

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
#include <ctype.h>

#include "sync.h"
#include "lbard.h"
#include "hf.h"
#include "radios.h"

int hfbarrett_initialise(int serialfd);

// Used for distinguishing between Codan and Barrett HF radios
unsigned char barrett_e0_string[6]={0x13,'E','0',13,10,0x11};

// This routine is shared between the Codan and Barrett HF radios,
// as we do a bit of clever work to check for a Barrett error string
// when sending a valid Codan command.
int hfcodanbarrett_radio_detect(int fd)
{
  // We require a serial port
  if (fd==-1) return -1;
  
  unsigned char buf[8192];
  unsigned clr[3]={21,13,10};
  int verhi,verlo;

  // Barrett radios can operate at either 9600 or 115200, so we need to check both
  serial_setup_port_with_speed(fd,115200);
  write_all(fd,clr,3); // Clear any partial command
  sleep(1); // give the radio the chance to respond
  ssize_t count = read_nonblock(fd,buf,8192);  // read and ignore any stuff
  write_all(fd,"VER\r",4); // ask Codan radio for version
  sleep(1); // give the radio the chance to respond
  count = read_nonblock(fd,buf,8192);  // read reply
  dump_bytes(stderr,"Response to VER command @ 115200 was",buf,count);
  int barrett_e0_seen=0;
  for(int i=0;i<=(count-6);i++)
    if (!memcmp(&buf[i],barrett_e0_string,6)) barrett_e0_seen=1;  

  if (!barrett_e0_seen) {
    serial_setup_port_with_speed(fd,9600);
    write_all(fd,clr,3); // Clear any partial command
    sleep(1); // give the radio the chance to respond
    ssize_t count = read_nonblock(fd,buf,8192);  // read and ignore any stuff
    write_all(fd,"VER\r",4); // ask Codan radio for version
    sleep(1); // give the radio the chance to respond
    count = read_nonblock(fd,buf,8192);  // read reply
    dump_bytes(stderr,"Response to VER command @ 9600 was",buf,count);
    for(int i=0;i<=(count-6);i++)
      if (!memcmp(&buf[i],barrett_e0_string,6)) barrett_e0_seen=1;
  }
  
  // If we get a version string -> Codan HF
  if (sscanf((char *)buf,"VER\r\nCICS: V%d.%d",&verhi,&verlo)==2) {
    fprintf(stderr,"Codan HF Radio running CICS V%d.%d\n",
	    verhi,verlo);
    if ((verhi>3)||((verhi==3)&&(verlo>=37)))
      // Codan radio supports ALE 3G (255 x 8-bit chars per message)
      radio_set_feature(RADIO_ALE_2G|RADIO_ALE_3G);
    else
      // Codan radio supports only ALE 2G (90 x 6-bit chars per message)
      radio_set_feature(RADIO_ALE_2G);
    radio_set_type(RADIOTYPE_HFCODAN);
    return 1; // indicate successfully initialised
  } else if (barrett_e0_seen) {
    fprintf(stderr,"Detected Barrett HF Radio.\n");
    radio_set_type(RADIOTYPE_HFBARRETT);
    radio_set_feature(RADIO_ALE_2G);

    return hfbarrett_initialise(fd);
  }
  return -1;
}

int hfcodan_serviceloop(int serialfd)
{
  char cmd[1024];
  
  switch(hf_state) {
  case HF_DISCONNECTED:
    // Currently disconnected. If the current time is later than the next scheduled
    // call-out time, then pick a hf station to call

    // Wait until we are allowed our first call before doing so
    if (time(0)<last_outbound_call) return 0;
    
    if ((hf_station_count>0)&&(time(0)>=hf_next_call_time)) {
      int next_station = hf_next_station_to_call();
      if (next_station>-1) {
	  snprintf(cmd,1024,"alecall %s \"!SERVAL,1,0,%s\"\r\n",
		   hf_stations[next_station].name,
		   radio_type_name(radio_get_type()));
	  write(serialfd,cmd,strlen(cmd));
	  hf_link_partner=next_station;
	  hf_state = HF_CALLREQUESTED|HF_COMMANDISSUED;
	  fprintf(stderr,"HF: Attempting to call station #%d '%s'\n",
		  next_station,hf_stations[next_station].name);
      }
    }
    break;
  case HF_CALLREQUESTED:
    break;
  case HF_CONNECTING:
    break;
  case HF_ALELINK:
    break;
  case HF_DISCONNECTING:
    break;
  default:
    break;
  }
  
  return 0;
}

int hfcodan_process_line(char *l)
{
  int channel,caller,callee,day,month,hour,minute;
  
  //  fprintf(stderr,"Codan radio (state 0x%04x) says: %s\n",hf_state,l);
  if (hf_state&HF_COMMANDISSUED) {
    // Ignore echoed commands, and wait for ">" prompt
    if (l[0]=='>') hf_state&=~HF_COMMANDISSUED;
    else if (!strcmp(l,"CALL STARTED")) {
      hf_state=HF_COMMANDISSUED|HF_CONNECTING;
    }
  }

  char fragment[8192];
  
  if (!strcmp(l,"AMD CALL STARTED")) ale_inprogress=1;
  else if (!strcmp(l,"CALL DETECTED")) {
    // Incoming ALE message -- so don't try sending anything for a little while
    hf_radio_pause_for_turnaround();
  } else if (!strcmp(l,"AMD CALL FINISHED")) ale_inprogress=0;
  else if (sscanf(l,"AMD-CALL: %d, %d, %d, %d/%d %d:%d, \"%[^\"]\"",
		  &channel,&caller,&callee,&day,&month,&hour,&minute,fragment)==8) {
    // Saw a fragment
    hf_process_fragment(fragment);
    // We must also by definition be connected
    hf_state=HF_ALELINK;
  } else if (sscanf(l,"ALE-LINK: %d, %d, %d, %d/%d %d:%d",
	     &channel,&caller,&callee,&day,&month,&hour,&minute)==7) {
    if (hf_link_partner>=-1)
      hf_stations[hf_link_partner].consecutive_connection_failures=0;
    ale_inprogress=0;
    if ((hf_state&0xff)!=HF_CONNECTING) {
      // We have a link, but without us asking for it.
      // So allow 10 seconds before trying to TX, else we start TXing immediately.
      hf_radio_pause_for_turnaround();
    } else hf_radio_mark_ready();

    fprintf(stderr,"ALE Link from %d -> %d on channel %d, I will send a packet in %ld seconds\n",
	    caller,callee,channel,
    	    hf_next_packet_time-time(0));

    hf_state=HF_ALELINK;    
  } else if ((!strcmp(l,"ALE-LINK: FAILED"))||(!strcmp(l,"LINK: CLOSED"))) {
    if (hf_state==HF_ALELINK) {
      // disconnected
    }
    if ((!strcmp(l,"ALE-LINK: FAILED"))||(hf_state!=HF_CONNECTING)) {
      if (hf_link_partner>-1) {
	// Mark link partner as having been attempted now, so that we can
	// round-robin better.  Basically we should probably mark the station we failed
	// to connect to for re-attempt in a few minutes.
	hf_stations[hf_link_partner].consecutive_connection_failures++;
	fprintf(stderr,"Failed to connect to station #%d '%s' (%d times in a row)\n",
		hf_link_partner,
		hf_stations[hf_link_partner].name,
		hf_stations[hf_link_partner].consecutive_connection_failures);
      }
      hf_link_partner=-1;
      ale_inprogress=0;

      // We have to also wait for the > prompt again
      hf_state=HF_DISCONNECTED|HF_COMMANDISSUED;
    }
  }
  
  return 0;
}

int hfcodan_receive_bytes(unsigned char *bytes,int count)
{ 
  int i;
  for(i=0;i<count;i++) {
    if (bytes[i]==13||bytes[i]==10) {
      hf_response_line[hf_rl_len]=0;
      if (hf_rl_len) hfcodan_process_line(hf_response_line);
      hf_rl_len=0;
    } else {
      if (hf_rl_len<1024) hf_response_line[hf_rl_len++]=bytes[i];
    }
  }
  
  return 0;
}

int hfcodan_send_packet(int serialfd,unsigned char *out, int len)
{
  // We can send upto 90 ALE encoded bytes.  ALE bytes are 6-bit, so we can send
  // 22 groups of 3 bytes = 66 bytes raw and 88 encoded bytes.  We can use the first
  // two bytes for fragmentation, since we would still like to support 256-byte
  // messages.  This means we need upto 4 pieces for each message.
  char message[8192];
  char fragment[8192];

  int i;
  time_t absolute_timeout=time(0)+90;

  if (hf_state!=HF_ALELINK) {
    fprintf(stderr,"Not sending packet, because we don't think we are in an ALE link.\n");
    return -1;
  }
  if (ale_inprogress) {
    fprintf(stderr,"Not sending packet, because we think an ALE transaction is already occurring.\n");
    return -1;
  }

  // How many pieces to send (1-6)
  // This means we have 36 possible fragment indications, if we wish to imply the
  // number of fragments in the fragment counter.
  int pieces=len/43; if (len%43) pieces++;
  
  fprintf(stderr,"Sending message of %d bytes via Codan HF\n",len);
  for(i=0;i<len;i+=43) {
    // Indicate radio type in fragment header
    fragment[0]=0x30+(hf_message_sequence_number&0x07);
    fragment[1]=0x30+(i/43);
    fragment[2]=0x30+pieces;
    int frag_len=43; if (len-i<43) frag_len=len-i;
    hex_encode(&out[i],&fragment[3],frag_len,radio_get_type());
    
    snprintf(message,8192,"amd %s\r\n",fragment);
    write_all(serialfd,message,strlen(message));

    int not_ready=1;
    while (not_ready) {
      if (time(0)>absolute_timeout) {
	fprintf(stderr,"Failed to send packet in reasonable amount of time. Aborting.\n");
	hf_message_sequence_number++;
	return -1;
      }

      usleep(100000);

      unsigned char buffer[8192];
      int count = read_nonblock(serialfd,buffer,8192);
      // if (count) dump_bytes(stderr,"postsend",buffer,count);
      if (count) hfcodan_receive_bytes(buffer,count);
      if (strstr((const char *)buffer,"AMD CALL FINISHED")) {
	not_ready=0;
	char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
	if (timestr[0]) timestr[strlen(timestr)-1]=0;
	fprintf(stderr,"  [%s] Sent %s",timestr,message);

      } else not_ready=1;
      if (strstr((const char *)buffer,"ERROR")) {
	// Something went wrong
	fprintf(stderr,"Error sending packet: Aborted.\n");
	hf_message_sequence_number++;
	return -1;
      }
      
    }    
  }
  
  hf_radio_pause_for_turnaround();
  hf_message_sequence_number++;
  char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
  if (timestr[0]) timestr[strlen(timestr)-1]=0;
  fprintf(stderr,"  [%s] Finished sending packet, next in %ld seconds.\n",
	  timestr,hf_next_packet_time-time(0));
  
  return 0;
}

