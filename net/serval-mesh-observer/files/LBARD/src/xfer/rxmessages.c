/*
Serval Low-bandwidth asychronous Rhizome Demonstrator.
Copyright (C) 2015 Serval Project Inc.

This program monitors a local Rhizome database and attempts
to synchronise it over low-bandwidth declarative transports, 
such as bluetooth name or wifi-direct service information
messages.  It is intended to give a high priority to MeshMS
converations among nearby nodes.

The design is fully asynchronous, so a call to the update_my_message()
function from time to time should be all that is required.


This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <assert.h>
#include <sys/time.h>

#include "sync.h"
#include "lbard.h"

extern char *my_sid_hex;
extern int my_time_stratum;

int saw_message(unsigned char *msg,int len,int rssi,char *my_sid,
		char *prefix, char *servald_server,char *credential)
{
  /*
    Parse message and act on it.    
  */
  
  // All valid messages must be at least 8 bytes long.
  if (len<8) return -1;
  char peer_prefix[6*2+1];
  snprintf(peer_prefix,6*2+1,"%02x%02x%02x%02x%02x%02x",
	   msg[0],msg[1],msg[2],msg[3],msg[4],msg[5]);
  int msg_number=msg[6]+256*(msg[7]&0x7f);
  int is_retransmission=msg[7]&0x80;

  // Ignore messages from ourselves
  if (!bcmp(msg,my_sid,6)) return -1;
  
  if (debug_pieces) {
    printf("Decoding message #%d from %s*, length = %d:\n",
	    msg_number,peer_prefix,len);
  }

  int offset=8; 

  int peer_index=-1;
  
  // Find or create peer structure for this.
  struct peer_state *p=NULL;
  for(int i=0;i<peer_count;i++) {
    if (!strcasecmp(peer_records[i]->sid_prefix,peer_prefix)) {
      p=peer_records[i]; peer_index=i; break;
    }
  }
  
  if (!p) {
    p=calloc(1,sizeof(struct peer_state));
    for(int i=0;i<4;i++) p->sid_prefix_bin[i]=msg[i];
    p->sid_prefix=strdup(peer_prefix);
    p->last_message_number=-1;
    p->tx_bundle=-1;
    p->request_bitmap_bundle=-1;
    printf("Registering peer %s*\n",p->sid_prefix);
    if (peer_count<MAX_PEERS) {
      peer_records[peer_count++]=p;      
    } else {
      // Peer table full.  Do random replacement.
      peer_index=random()%MAX_PEERS;
      free_peer(peer_records[peer_index]);
      peer_records[peer_index]=p;
    }
  }
  
  // Update time stamp and most recent message from peer
  if (msg_number>p->last_message_number) {
    // We probably have missed packets.
    // But only count if gap is <256, since more than that probably means
    // something more profound has happened.
    p->missed_packet_count+=msg_number-p->last_message_number-1;
  }
  p->last_message_time=time(0);
  if (!is_retransmission) p->last_message_number=msg_number;

  // Update RSSI log for this sender
  p->rssi_accumulator+=rssi;
  p->rssi_counter++;

  // Log recently received packets, so that we can show RSSI history for received packets
  log_rssi(p,rssi);	 
  
  while(offset<len) {
    if (debug_pieces||debug_message_pieces) {
      printf(
	      "Saw message section with type '%c' (0x%02x) @ offset $%02x, len=%d\n",
	      msg[offset],msg[offset],offset,len);
      fflush(stderr);
    }

    if (message_handlers[msg[offset]]) {
      if (debug_pieces)
	printf("### %s : Calling message handler for type '%c' @ offset 0x%x\n",
	       timestamp_str(),msg[offset],offset);
      int advance=message_handlers[msg[offset]](p,peer_prefix,servald_server,credential,
						&msg[offset],len-offset);
      if (advance<1) {
	fprintf(stderr,
		"At packet offset 0x%x, message parser 0x%02x returned zero or negative message length (=%d).\n"
		"  Assuming packet is corrupt.\n",
		offset,msg[offset],advance);
	return -1;
      } else {
	if (debug_pieces)
	  printf("### %s : Handler consumed %d packet bytes.\n",timestamp_str(),advance);
	offset+=advance;
      }
    } else {
      // No parser for this message type
      // invalid message field.
      if (monitor_mode)
	{
	  char sender_prefix[128];
	  char monitor_log_buf[1024];
	  sprintf(sender_prefix,"%s*",p->sid_prefix);
	  
	  snprintf(monitor_log_buf,sizeof(monitor_log_buf),
		   "Illegal message field 0x%02X at radio packet offset %d",
		   msg[offset],offset);
	  
	  monitor_log(sender_prefix,NULL,monitor_log_buf);
	}

      if (debug_pieces)
	printf("### %s : No message handler for type '%c' @ offset 0x%x -- stopping processing of packet.\n",
	       timestamp_str(),msg[offset],offset);
      return -1;
    }
  }
  return 0;
}
