
/*
The following specially formatted comments tell the LBARD build environment about this radio.
See radio_type for the meaning of each field.
See radios.h target in Makefile to see how this comment is used to register support for the radio.

RADIO TYPE: HFBARRETT,"hfbarrett","Barrett HF with ALE",hfcodanbarrett_radio_detect,hfbarrett_serviceloop,hfbarrett_receive_bytes,hfbarrett_send_packet,hfbarrett_my_turn_to_send,20

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

char barrett_link_partner_string[1024]="";
int previous_state=-1;
unsigned char pause_tx=0x013; //XOFF by default
time_t ALElink_establishment_time=30; //should get it from the alias?
int message_failure=0;
int ale_transmission=-1;
int ale_command_state=-1;
// -1: unknown
//  0: not executed
//  1: executed
//  2: understood by the radio, but nothing has happened

int hfbarrett_my_turn_to_send(void)
{
  if (hfbarrett_ready_test())
    return hf_radio_check_if_ready();
  else
    return 0;
}

int hfbarrett_ready_test(void)
{
  int isReady=1;
  
  if (hf_state!=HF_ALELINK) isReady=0;
  if (ale_inprogress) isReady=0;
  if (!barrett_link_partner_string[0]) isReady=0;

  return isReady; 
}

int hfbarrett_initialise(int serialfd)
{
  // See "2050 RS-232 ALE Commands" document from Barrett for more information on the
  // available commands.
  
  // XXX - Issue AXENABT to enable ALE?
  // XXX - Issue AXALRM0 to disable audible alarms when operating? (or 1 to enable them?)
  // XXX - Issue AICTBL to get current ALE channel list?
  // XXX - Issue AIATBL to get current ALE address list
  //       (and use a "serval" prefix on the 0-15 char alias names to auto-pick the parties to talk to?)
  //       (or should it be AINTBL to get ALE network addresses?)
  // XXX - Issue AISTBL to get channel scan list?
  // XXX - Issue ARAMDM1 to register for ALE AMD message notifications?
  // XXX - Issue ARLINK1 to register for ALE LINK notifications?
  // XXX - ARLTBL1 to register for ALE LINK table notifications?
  // XXX - ARMESS1 to register for ALE event notifications?
  // XXX - ARSTAT1 to register for ALE status change notifications?

    
  // Tell Barrett radio we want to know when various events occur.
  char *setup_string[10]
    ={
		"AIATBL\r\n", // Ask for all valid ale addresses
    "ARAMDM1\r\n", // Register for AMD messages
    "ARAMDP1\r\n", // Register for phone messages
    "ARCALL1\r\n", // Register for new calls
    "ARLINK1\r\n", // Hear about ALE link notifications
    "ARLTBL1\r\n", // Hear about ALE link table events
    "ARMESS1\r\n", // Hear about ALE event notifications
    "ARSTAT1\r\n", // Hear about ALE status change notifications
		"AXALRM0\r\n", // Diable alarm sound when receiving a call
		"AILTBL\r\n", // Ask for the current ALE link state
  };
  int i;
  for(i=0; i<10; i++) {
    write(serialfd,setup_string[i],strlen(setup_string[i]));
    usleep(200000);
  }    
  
  return 1;
}

int hfbarrett_serviceloop(int serialfd)
{
	char cmd[1024];
	unsigned char buffer[8192];
  int count;
  int time_to_abort;

  switch(hf_state) {

  case HF_DISCONNECTED: //1
  
    // The call requested has to abort, the radio is receiving a another call
    if (previous_state==HF_CALLREQUESTED){
      printf("Aborting call\n");
      write_all(serialfd,"AXABORT\r\n",9);
    }
    
    // Wait until we are allowed our first call before doing so
    if (time(0)<last_outbound_call) return 0;
    
    
    // Currently disconnected. If the current time is later than the next scheduled
    // If the radio is not receiving a message
    // call-out time, then pick a hf station to call

    if ((ale_inprogress==0)&&(hf_link_partner==-1)&&(hf_station_count>0)&&(time(0)>=hf_next_call_time)) {
      int next_station = hf_next_station_to_call();
      if (next_station>-1) {
			  // Ensure we have a clear line for new command (we were getting some
			  // errors here intermittantly).				
			  write(serialfd,"\r\n",2);

			  // The AXLINK commmand creates a link which is unusable. So we connect sending the message CONNECTED.
			  // A link will be established after the mesage is receied.
			  init_buffer((unsigned char*)cmd, 1024);
			  snprintf(cmd,1024,"AXNMSG%s%sCONNECTING\r\n", hf_stations[next_station].index, self_hf_station.index);
			  //printf("sending '%s' to try to make ALE call.\n",cmd);			  
			  
	      // We add a random 0 - 4 seconds to avoid lock-step failure modes,
        // e.g., where both radios keep trying to talk to each other at
        // the same time.
			  sleep(random()%4);
			  
			  //Update the radio replies before sending cmd
				count = read_nonblock(serialfd,buffer,8192);
		  	if (count) hfbarrett_receive_bytes(buffer,count);
			  
			  // Recheck that, after the sleep(random), the radio is still idle
			  // before sending
			  if (ale_inprogress==0){
			    write(serialfd,cmd,strlen(cmd));

			    hf_state = HF_CALLREQUESTED;
					
			    fprintf(stderr,"HF: Attempting to call station #%d '%s'\n",
		    next_station,hf_stations[next_station].name);
        	hf_next_call_time=time(0)+ALElink_establishment_time;
        }else{
          printf("The radio is not idle. The call request is not sent.\n");
        }
		  }
    }
    else if (hf_link_partner>-1)
      hf_state=HF_ALELINK;
	  // Probe periodically with AILTBL to get link table, because the modem doesn't
    // preemptively tell us when we get a link established 
	  else if (time(0)!=last_link_probe_time) { //once a second
      write(serialfd,"AILTBL\r\n",8);
      last_link_probe_time=time(0);
    }
  
    break;

  case HF_CALLREQUESTED: //2
		// Probe periodically with AILTBL to get link table, because the modem doesn't
    // preemptively tell us when we get a link established
    if (time(0)!=last_link_probe_time)  { //once a second
      //write(serialfd,"AILTBL\r\n",8);
      last_link_probe_time=time(0);
    }
    if (ale_inprogress==2){
      printf("Another radio is calling. Marking this sent call disconnected\n");
      hf_state=HF_DISCONNECTED;
    }
		if (time(0)>=hf_next_call_time){ //no reply from the called station
			hf_state = HF_DISCONNECTED;
			printf("Make the call disconnected because of no reply\n");
		}
    break;

  case HF_CONNECTING: //3
		
    break;

  case HF_ALELINK: //4
		// Probe periodically with AILTBL to get link table, because the modem doesn't
    // preemptively tell us when we lose a link
		
		// Also in the case where previsou_state=-1
		// because previous_state is only updeated at the end of the first service loop.
		// So we are sure to cover the case of the first loop.
    if ((previous_state==HF_DISCONNECTED)||(previous_state==-1)||previous_state==HF_ALESENDING){
      printf("Let the other side send messages\n");
      hf_radio_pause_for_turnaround();
    }
		
		if (message_failure>10){
		  printf("Receiving message failed more than 10 times. The link is not good. Reset radio\n");
		  hf_state=HF_RADIOCONFUSED;
		  message_failure=0;
		  return -1;
		}
		
    if (time(0)!=last_link_probe_time)  { //once a second
			last_link_probe_time=time(0);
    }
    
    if (previous_state!=HF_ALELINK){
      fprintf(stderr,"Radio linked with %s (station #%d), I will send a packet in %ld seconds\n",
      barrett_link_partner_string,hf_link_partner,
      hf_next_packet_time-time(0));
    }

    
    break;

  case HF_DISCONNECTING: //5
    //*****************
    // Unused for now, but could be useful at the end of a exchange between LBARDs
    //*****************
    // Aborting ALE link nb 00.
    // As LBARD established only one ALE link,
    // it is the same thing as saying: abroting ALE link
    
    printf("Aborting the current established ALE link\n");
    while (hf_link_partner>-1){
      time_to_abort=0;
      ale_command_state=0;
      write_all(serialfd,"AXTLNK00\r\n",10);
      while(ale_command_state==0){
        sleep(1); //once a second
        write_all(serialfd,"AILTBL\r\n",8);
			  init_buffer(buffer, 8192);
			  count = read_nonblock(serialfd,buffer,8192);
			  
			  // Looking at the radio responses and process line(s):
			  if (count) hfbarrett_receive_bytes(buffer,count);
        
			  if (time_to_abort>=20){
			    ale_command_state=2; //command executed but nothing has happened
			    printf("Aborting failed. Retrying to abort again.\n");
			  }
			  time_to_abort++;
		  }
	  }
	  hf_state=HF_DISCONNECTED;
	  printf("Let time to the other radio to terminate\n");
		sleep(10); //Let time to the other size to terminate
    
    break;

	case HF_ALESENDING: //6
    // This state is managed in the hfbarret_send_packet function
		break;

  case HF_RADIOCONFUSED: //7
    // We entenred a case where the radio is confused
    // Sending "*" reboots the radio
    printf("Rebooting the radio.\n");
    write(serialfd,"*",1);
    hf_state=HF_DISCONNECTED;
    sleep(10);
    
    break;

  default:
		
    break;
  }

	if (previous_state!=hf_state){
		fprintf(stderr,"\nBarrett radio changed to state 0x%04x\n",hf_state);
		previous_state=hf_state;
  }
  return 0;
}

int hfbarrett_process_line(char *l)
{
  // Skip XON/XOFF character at start of line
  while(l[0]&&l[0]<' ') l++;
  while(l[0]&&(l[strlen(l)-1]<' ')) l[strlen(l)-1]=0;
  
  //fprintf(stderr,"Barrett radio says (in state 0x%04x): %s\n",hf_state,l);

  if ((!strcmp(l,"EV00"))&&(hf_state==HF_CALLREQUESTED)) {
    // Syntax error in our request to call.
    printf("Saw EV00 response. Marking call disconnected.\n");
		hf_next_call_time=time(0); //AXLINK failed, no call have been tried
    hf_state = HF_DISCONNECTED;
    return 0;
  }
  if ((!strcmp(l,"E0"))&&(hf_state==HF_CALLREQUESTED)) {
    // Syntax error in our request to call.
    printf("Saw E0 response. Marking call disconnected.\n");
		hf_next_call_time=time(0); //AXLINK failed, no call have been tried
    hf_state = HF_DISCONNECTED;
    return 0;
  }
	if ((!strcmp(l,"EV08"))&&(hf_state==HF_CALLREQUESTED)) {
    // Syntax error in our request to call.
    printf("Saw EV08 response. Marking call disconnected.\n");
		hf_state = HF_DISCONNECTED;
    return 0;
  }

  char tmp[8192];

	if (sscanf(l, "AIATBL%s", tmp)==1){ 


		hf_parse_linkcandidate(l);
		
		//display all the hf radios
		printf("The self hf Barrett radio is: \n%s, index=%s\n", self_hf_station.name, self_hf_station.index);		
		printf("The registered stations are:\n");		
		int i;		
		for (i=0; i<hf_station_count; i++){
			printf("%s, index=%s\n", hf_stations[i].name, hf_stations[i].index);
		}
	}

  if (sscanf(l,"AIAMDM%s",tmp)==1) {
    fprintf(stderr,"Barrett radio saw ALE AMD message '%s'\n",&tmp[6]);
    message_failure=0;
    hf_process_fragment(&tmp[6]);
  }

	if (sscanf(l, "AISTAT%s", tmp)==1){
		//idle
		if (tmp[1]=='0'){
			ale_inprogress=0;
		}
		//call transmitting
		if (tmp[1]=='1'){
		  ale_inprogress=1;
		}
		//call receiving
		if (tmp[1]=='2'){
		  ale_inprogress=2;
		}
		if (tmp[2]=='0'){
		  ale_transmission=0;
		}
		if (tmp[2]=='1'){
		  ale_transmission=1;
		}
		if ((tmp[1]=='2')&&(tmp[2]=='0')&&(hf_state==HF_ALELINK)){
		  printf("Turn idle after receiving\n");
		  message_failure++;
		}
	}

  if ((!strcmp(l,"AILTBL"))&&((hf_state==HF_ALELINK)||(hf_state==HF_ALESENDING))) {
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

    // We have to also wait for the > prompt again
    printf("Timed out trying to connect. Marking call disconnected.\n");
    hf_state=HF_DISCONNECTED;
  }
  
	if ((sscanf(l,"AILTBL%s",tmp)==1)&&(hf_state!=HF_ALELINK)&&(hf_state!=HF_DISCONNECTING)&&(hf_state!=HF_ALESENDING)) {

    // Link established
    barrett_link_partner_string[0]=tmp[4];
    barrett_link_partner_string[1]=tmp[5];
    barrett_link_partner_string[2]=tmp[2];
    barrett_link_partner_string[3]=tmp[3];
    barrett_link_partner_string[4]=0;

    int i;
    for(i=0;i<hf_station_count;i++){
		  strcpy(tmp, hf_stations[i].index);
		  strcat(tmp, self_hf_station.index);
      if (!strcmp(barrett_link_partner_string, tmp)){ 
			  hf_link_partner=i;
			  hf_stations[hf_link_partner].consecutive_connection_failures=0;
		    break; 
		  }
	  }

    if (((hf_state&0xff)!=HF_CONNECTING)
  &&((hf_state&0xff)!=HF_CALLREQUESTED)) {
      // We have a link, but without us asking for it.
      // So allow 10 seconds before trying to TX, else we start TXing immediately.
    //hf_radio_pause_for_turnaround(); //done in HF_ALELINK state serviceloop 
    } else hf_radio_mark_ready();
      
    hf_state=HF_ALELINK;   
  
  }
  
  if ((!strcmp(l,"AIMESS3"))&&(hf_state==HF_CALLREQUESTED)){
    printf("No link established after the call request\n");
    hf_state=HF_DISCONNECTED;
    
  }
  
  if ((hf_state==HF_DISCONNECTING)&&(!strcmp(l,"AILTBL"))){
    hf_link_partner=-1;
    ale_command_state=1; //we know the link has been aborted
  }
  
  if ((hf_state==HF_ALESENDING)&&(!strcmp(l,"AIMESS1"))){
		ale_command_state=1;
  }

  return 0;
}

int hfbarrett_receive_bytes(unsigned char *bytes,int count)
{
  int i;
  for(i=0;i<count;i++) {
    if (bytes[i]==0x011){
			//printf("XON\n");
			pause_tx=0x011;
		}
		if (bytes[i]==0x013){
			//printf("XOFF\n");
			pause_tx=0x013;
		}
    if (bytes[i]==13||bytes[i]==10) { //end of command detected => if not null, line is processed by lbard
      hf_response_line[hf_rl_len]=0; //	after the command we out a '\0' to have a proper string
      if (hf_rl_len){ hfbarrett_process_line(hf_response_line);}
      hf_rl_len=0;
    } else {
      if (hf_rl_len<1024) hf_response_line[hf_rl_len++]=bytes[i];
    }
  }
  return 0;
}

int hfbarrett_send_packet(int serialfd,unsigned char *out, int len)
{
  // We can send upto 90 ALE encoded bytes.  ALE bytes are 6-bit, so we can send
  // 22 groups of 3 bytes = 66 bytes raw and 88 encoded bytes.
  // XXX - In practice we get only 4 bits per byte, as we don't seem to be able
  // to get 6-bit clean. But recheck with Barrett to Barrett, as it might be a
  // Barrett / Codan interoperabilitty problem.
  // We can use the first
  // two bytes for fragmentation, since we would still like to support 256-byte
  // messages.  This means we need upto 4 pieces for each message.
  char message[8192];
  char fragment[8192];

  int i;

  time_t absolute_timeout=time(0)+200;

  if (!hfbarrett_ready_test()) return -1;
  
  // How many pieces to send (1-6)
  // This means we have 36 possible fragment indications, if we wish to imply the
  // number of fragments in the fragment counter.
  int pieces=len/43; if (len%43) pieces++;
  
  previous_state=hf_state; //necessary because LBARD will nor run the service loop while being in HF_ALESENDING state
  hf_state=HF_ALESENDING;
  
  fprintf(stderr,"Sending message of %d bytes via Barratt HF\n",len);
  for(i=0;i<len;i+=43) {
		//printf("Nb of loops=%d\n", i);
    // Indicate radio type in fragment header
    fragment[0]=0x41+(hf_message_sequence_number&0x07);
    fragment[1]=0x30+(i/43);
    fragment[2]=0x30+pieces;
    int frag_len=43; if (len-i<43) frag_len=len-i;
    hex_encode(&out[i],&fragment[3],frag_len,radio_get_type());

    unsigned char buffer[8192];

    int count;

    usleep(100000);
    count = read_nonblock(serialfd,buffer,8192);

    //if (count) dump_bytes(stderr,"presend",buffer,count);
    if (count) hfbarrett_receive_bytes(buffer,count);
    
    snprintf(message,8192,"AXNMSG%s%02d%s\r\n",
	     barrett_link_partner_string,
	     (int)strlen(fragment),fragment);

		int time_to_send_frag;
		ale_command_state=0;
    while (ale_command_state!=1) {
      if (time(0)>absolute_timeout) {
	fprintf(stderr,"Failed to send packet in reasonable amount of time. Aborting.\n");
	hf_message_sequence_number++;
	previous_state=hf_state; //necessary because LBARD will nor run the service loop while being in HF_ALESENDING state
	hf_state=HF_ALELINK;
	return -1;
      }
      
      // Check if there if we reveived an XON from the radio
			count = read_nonblock(serialfd,buffer,8192);
			if (count) hfbarrett_receive_bytes(buffer,count);
			if (pause_tx==0x011){
			  
			  //Send command to send a message
		    printf("Atempting to send one fragment: %s", message);

	      // We add a random 0 - 4 seconds to avoid lock-step failure modes,
        // e.g., where both radios keep trying to talk to each other at
        // the same time.
			  sleep(random()%4);
			  
			  //Update the radio replies before sending cmd
				count = read_nonblock(serialfd,buffer,8192);
		  	if (count) hfbarrett_receive_bytes(buffer,count);
			  
			  // Recheck that, after the sleep(random), the radio is still idle
			  // before sending
			  if (ale_inprogress==0)
  		    write_all(serialfd,message,strlen(message));
		    else if (ale_inprogress==2){
		      printf("The radio is receiving a call. Leaving ALESENDING state to ALELINK.\n");
		      previous_state=hf_state; //necessary because LBARD will nor run the service loop while being in HF_ALESENDING state
		      hf_state=HF_ALELINK;
		      write_all(serialfd,"AXABORT\r\n",9);
		      //hf_radio_pause_for_turnaround();
		      return -1;
	      }

        count=-1;

				// Look at the ALE indications sent byt the radio after the command to send 
				// a message was sent until we know if the message has been sent or if it hasn't
				time_to_send_frag=0;
				ale_command_state=0;
				while(ale_command_state==0){
				
				  if(time_to_send_frag>60){
				    printf("Something wrong occured with the radio.\n");
				    previous_state=hf_state; //necessary because LBARD will nor run the service loop while being in HF_ALESENDING state
				    hf_state=HF_RADIOCONFUSED; 
				    return -1; 
				  }
				
				  //printf("Sleeping for upto %d more seconds while waiting for message to TX.\n",ale_command_state);
					// Any ALE send will take a while, check the radio's responses every second
					sleep(1);

					init_buffer(buffer, 8192);
					count = read_nonblock(serialfd,buffer,8192);
					//if (count) dump_bytes(stderr,"received",buffer,count);
					if (count) hfbarrett_receive_bytes(buffer,count);
					//printf("buffer is: %s\n", buffer);
					//if (strstr((const char *)buffer,"AIMESS1")){
					if (ale_command_state==1){
						char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
		 				if (timestr[0]) timestr[strlen(timestr)-1]=0;
						fprintf(stderr,"  [%s] Sent %s",timestr,message);
					}
					else if (ale_inprogress==0){
					  printf("Radio turned into idle before sending message. Aborting message and try again.\n");
						write_all(serialfd,"AXABORT\r\n",9);
						ale_command_state=2;
					}
					else if (ale_inprogress==2){
		        ale_command_state=2;
		        printf("While trying to send a message, another message is received. Abort sending message and pause to listen to this message\n");
		        write_all(serialfd,"AXABORT\r\n",9);
		        previous_state=hf_state; //necessary because LBARD will nor run the service loop while being in HF_ALESENDING state
		        hf_state=HF_ALELINK;
		        //hf_radio_pause_for_turnaround();
		        return -1;
	        }
	                
					time_to_send_frag++;
				}        
			
      sleep(3); // Let time to the radio to be ready for the next step

  
    	} else{
				printf("\nThe radio is not ready to receive a command (XOFF)\n ");				
				sleep(1);	// Let time to the radio to empty its internal buffer
			}
		}
  }
  // The whole message has been sent (all the fragments)
  //hf_radio_pause_for_turnaround(); //Done in the service loop
  previous_state=hf_state; //necessary because LBARD will nor run the service loop while being in HF_ALESENDING state
  hf_state=HF_ALELINK;
  hf_message_sequence_number++;
  char timestr[100]; time_t now=time(0); ctime_r(&now,timestr);
  if (timestr[0]) timestr[strlen(timestr)-1]=0;
  fprintf(stderr,"  [%s] Finished sending packet, next in %ld seconds.\n",
	  timestr,hf_next_packet_time-time(0));
  
  return 0;
}

int init_buffer(unsigned char* buffer, int size){
	int i;
	for(i=0; i<size; i++){
		if (buffer[i]!=0)
			buffer[i]=0;
	}
	return 0; 
}
