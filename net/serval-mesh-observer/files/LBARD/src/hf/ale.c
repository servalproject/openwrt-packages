/*
  Manage the connected HF radio according to the supplied configuration.

  Configuration files look like:

10% calling duty cycle 
station "101" 5 minutes every 2 hours
station "102" 5 minutes every 2 hours
station "103" 5 minutes every 2 hours

  The calling duty cycle is calculated on an hourly basis, and counts only connected
  time. Call connections will be limited to 20% of the time, so that there is ample
  opportunity for a station to listen for incoming connections.

  A 100% duty cycle will mean that this radio will never be able to receive calls,
  so a 50% duty cycle (or better 1/n) duty cycle is probably more appropriate.

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

extern unsigned char my_sid[32];
extern char *my_sid_hex;
extern char *servald_server;
extern char *credential;
extern char *prefix;

int hf_state=HF_DISCONNECTED;
int hf_link_partner=-1;

time_t hf_next_call_time=0;

time_t last_link_probe_time=0;

time_t hf_next_packet_time=0;
time_t last_outbound_call=0;

int hf_callout_duty_cycle=0;
int hf_callout_interval=5; // minutes

struct hf_station hf_stations[MAX_HF_STATIONS];
int hf_station_count=0;
struct hf_station self_hf_station;
time_t timeout_call_a_radio_again = 100;

int has_hf_plan=0;

time_t last_ready_report_time=0;

int ale_inprogress=0;
int hf_rl_len=0;
char hf_response_line[1024];

int hf_message_sequence_number=0;

char *radio_type_name(int radio_type)
{
  for(int i=0;radio_types[i].id!=-1;i++)
    if (i==radio_type) return radio_types[i].name;
 return "Unknown";
}

char *radio_type_description(int radio_type)
{
  for(int i=0;radio_types[i].id!=-1;i++)
    if (i==radio_type) return radio_types[i].description;
 return "Unknown";
}

int hf_radio_check_if_ready(void)
{  
  if (time(0)>=hf_next_packet_time) {
    if (time(0)!=last_ready_report_time) {
      char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
      if (timestr[0]) timestr[strlen(timestr)-1]=0;
      if (hf_state==HF_ALELINK){
	      fprintf(stderr,"  [%s] HF Radio cleared to transmit.\n",
		timestr);
		  }
    }
    last_ready_report_time=time(0);
    return 1;
  } else {
    if (time(0)!=last_ready_report_time) {
      char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
      if (timestr[0]) timestr[strlen(timestr)-1]=0;
      //fprintf(stderr,"  [%s] Wait %ld more seconds to allow other side to send.\n",
	      //timestr,hf_next_packet_time-time(0));
    }
    last_ready_report_time=time(0);
    return 0;
  }
}

int hf_next_station_to_call(void)
{
  int i;
  for(i=0;i<hf_station_count;i++) {
    if (time(0)>hf_stations[i].next_link_time){ 
			hf_stations[i].next_link_time = time(0) + timeout_call_a_radio_again;
			return i;
		}
  }
  if (hf_station_count) return random()%hf_station_count; else return -1;
}

int hf_radio_mark_ready(void)
{
  hf_next_packet_time=0;
  char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
  if (timestr[0]) timestr[strlen(timestr)-1]=0;
  fprintf(stderr,"  [%s] It is our turn to send.\n",timestr);
  return 0;

}

int hf_radio_pause_for_turnaround(void)
{
  int radio_type=radio_get_type();
  if (radio_type<0) return -1;

  // We add a random 1 - 10 seconds to avoid lock-step failure modes,
  // e.g., where both radios keep trying to talk to each other at
  // the same time.
  hf_next_packet_time=time(0)+radio_types[radio_get_type()].hf_turnaround_delay+(random()%10)+30; //+30 for debug, time to detect for the transmitting radio if message is sent

  fprintf(stderr,"  [%s] Delaying %ld seconds to allow other side to send.\n",
	  timestamp_str(),hf_next_packet_time-time(0));
  
  return 0;
}

int pieces_seen[6]={0,0,0,0,0,0};
unsigned char accummulated_packet[256];


int hf_process_fragment(char *fragment)
{
  int peer_radio=-1;
  int sequence=-1;
  if ((fragment[0]>='0')&&(fragment[0]<='7')) {
    peer_radio=RADIOTYPE_HFCODAN;
    sequence=fragment[0]-'0';
  }
  if ((fragment[0]>='A')&&(fragment[0]<='H')) {
    peer_radio=RADIOTYPE_HFBARRETT;
    sequence=fragment[0]-'A';
  }
  int piece_number=(fragment[1]-'0');
  int pieces=(fragment[2]-'0');

  fprintf(stderr,"Checking if message is a fragment (piece %d/%d, peer=%d).\n",
	  piece_number,pieces,peer_radio);
  if (peer_radio<0) return -1;
  if (pieces<1||pieces>6) return -1;
  if (piece_number<0||piece_number>5) return -1;
  fprintf(stderr,"Received piece %d/%d of packet sequence #%d from a %s radio.\n",
	  piece_number+1,pieces,sequence,radio_type_name(peer_radio));

  int packet_offset=piece_number*43;
  int i;
  for(i=3;i<strlen(fragment);i+=2) {
    if (ishex(fragment[i+0])&&ishex(fragment[i+1])) {
      int v=(chartohexnybl(fragment[i+0])<<4)+chartohexnybl(fragment[i+1]);
      accummulated_packet[packet_offset++]=v;
    }
  }
  if (piece_number==(pieces-1)) {
    // We have a terminal piece: so assume we have the whole packet.
    // (the FEC will reject it if it is incorrectly assembled).
    fprintf(stderr,"Passing reassembled packet of %d bytes up for processing.\n",
	    packet_offset);
    saw_packet(accummulated_packet,packet_offset,0 /* RSSI unknown */,
	       my_sid_hex,prefix,servald_server,credential);

    // Now it is our turn to send
    hf_radio_mark_ready();
  } else
    // Not end of packet, wait 8+1d8 seconds before we try transmitting.
    hf_radio_pause_for_turnaround();
  
  return 0;
}

