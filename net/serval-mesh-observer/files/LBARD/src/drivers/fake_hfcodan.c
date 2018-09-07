#include "fakecsmaradio.h"

long long codan_starttime=0;

void codan_prompt(int client)
{
  char prompt[1024];
  int hours,minutes,seconds,msec;
  if (!codan_starttime) {
    codan_starttime=gettime_ms();
  }
  long long elapsed=gettime_ms()-codan_starttime;
  msec=elapsed%1000;  elapsed/=1000;
  seconds=elapsed%60; elapsed/=60;
  minutes=elapsed%60; elapsed/=60;
  hours=elapsed%24;
  
  snprintf(prompt,1024,"%02d:%02d:%02d.%03d> ",
	   hours,minutes,seconds,msec);
  write(clients[client].socket,prompt,strlen(prompt));
  fprintf(stderr,"Sending CODAN prompt '%s'\n",prompt);
  return;
}     

int hfcodan_read_byte(int i,unsigned char c)
{
  if (c==0x15) {
    // Control-U -- clear input buffer
    clients[i].buffer_count=0;
  } else if ((c!='\n')&&(c!='\r')&&c) {
    // fprintf(stderr,"Radio #%d received character 0x%02x\n",i,c);

    // First echo the character back
    write(clients[i].socket,&c,1);
    
    if (clients[i].buffer_count<(CLIENT_BUFFER_SIZE-1))
      clients[i].buffer[clients[i].buffer_count++]=c;
  } else {
    if (clients[i].buffer_count) {

      // Print CRLF
      write(clients[i].socket,"\r\n",2);
      
      clients[i].buffer[clients[i].buffer_count]=0;
      fprintf(stderr,"Codan HF Radio #%d sent command '%s'\n",i,clients[i].buffer);

      // Process the command here
      if (!strcasecmp("VER",(char *)clients[i].buffer)) {
	// Claim to be an ALE 3G capable radio
	write(clients[i].socket,"CICS: V3.37\r\n",
	      strlen("CICS: V3.37\r\n"));
      } else {
	// Complain about unknown commands
	write(clients[i].socket,
	      "ERROR: Command not recognised\r\n",
	      strlen("ERROR: Command not recognised\r\n"));
      }

      // Display a Codan like prompt
      codan_prompt(i);

      // Reset buffer ready for next command
      clients[i].buffer_count=0;
    }    
  }

  return 0;
}

int hfcodan_heartbeat(int client)
{
  return 0;
}

int hfcodan_encapsulate_packet(int from,int to,
			       unsigned char *packet,
			       int *packet_len)
{
  return 0;
}
