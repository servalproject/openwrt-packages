/*
Serval Low-bandwidth asychronous Rhizome Demonstrator.
Copyright (C) 2015-2018 Serval Project Inc., Flinders University.

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

int append_generationid(unsigned char *msg_out,int *offset)
{
  // G + 4 random bytes = 5 bytes
  struct timeval tv;
  gettimeofday(&tv,NULL);    
  
  msg_out[(*offset)++]='G';
  for(int i=0;i<4;i++) msg_out[(*offset)++]=(my_instance_id>>(i*8))&0xff;
  return 0;
}

int message_parser_47(struct peer_state *sender,char *sender_prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  // Get instance ID of peer. We use this to note if a peer's lbard has restarted
  int offset=0;
  offset++;
  {
    unsigned int peer_instance_id=0;
    for(int i=0;i<4;i++) peer_instance_id|=(msg[offset++]<<(i*8));
    if (!sender->instance_id) sender->instance_id=peer_instance_id;
    if (sender->instance_id!=peer_instance_id) {
      // Peer's instance ID has changed: Forget all knowledge of the peer and
      // return (ignoring the rest of the packet).
#ifndef SYNC_BY_BAR
      int peer_index=-1;
      for(int i=0;i<peer_count;i++) if (sender==peer_records[i]) { peer_index=i; break; }
      if (peer_index==-1) {
	// Could not find peer structure. This should not happen.
	return 0;
      }
      
      free_peer(peer_records[peer_index]);
      sender=calloc(1,sizeof(struct peer_state));
      for(int i=0;i<4;i++) sender->sid_prefix_bin[i]=msg[i];
      sender->sid_prefix=strdup(sender_prefix);
      sender->last_message_number=-1;
      sender->tx_bundle=-1;
      sender->instance_id=peer_instance_id;
      printf("Peer %s* has restarted -- discarding stale knowledge of its state.\n",sender->sid_prefix);
      peer_records[peer_index]=sender;
#endif
    }
  }
  
  return offset;
}

