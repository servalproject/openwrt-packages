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

int announce_bundle_length(int mtu, unsigned char *msg,int *offset,
			   unsigned char *bid_bin,long long version,unsigned int length)
{
  if ((mtu-*offset)>(1+8+8+4)) {
    // Announce length of bundle
    msg[(*offset)++]='L';
    // Bundle prefix (8 bytes)
    for(int i=0;i<8;i++) msg[(*offset)++]=bid_bin[i];
    // Bundle version (8 bytes)
    for(int i=0;i<8;i++) msg[(*offset)++]=(version>>(i*8))&0xff;
    // Length (4 bytes)
    msg[(*offset)++]=(length>>0)&0xff;
    msg[(*offset)++]=(length>>8)&0xff;
    msg[(*offset)++]=(length>>16)&0xff;
    msg[(*offset)++]=(length>>24)&0xff;
  }
  return 0;
}

int saw_length(char *peer_prefix,char *bid_prefix,long long version,
	       int body_length)
{
  // Note length of payload for this bundle, if we don't already know it
  int peer=find_peer_by_prefix(peer_prefix);
  if (peer<0) return -1;

  int i;
  int spare_record=random()%MAX_BUNDLES_IN_FLIGHT;
  for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
    if (!partials[i].bid_prefix) {
      if (spare_record==-1) spare_record=i;
    } else {
      if (!strcasecmp(partials[i].bid_prefix,bid_prefix))
	if (partials[i].bundle_version==version)
	  {
	    partials[i].body_length=body_length;
	    return 0;
	  }
    }
  }
  return -1;
}

int message_parser_4C(struct peer_state *sender,char *sender_prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  // Get instance ID of peer. We use this to note if a peer's lbard has restarted
  int offset=0;

  if ((length-offset)<(1+8+8+4)) {
    fprintf(stderr,"Error parsing message type 0x4C at offset 0x%x: length-offset=%d-%d=%d, but expected at least 1+8+8+4=21 bytes.\n",
	    offset,length,offset,length-offset);
    dump_bytes(stderr,"complete packet",msg,length);
    return -3;
  }

  offset++;
  
  int bid_prefix_offset=offset;
  char bid_prefix[2*8+1+1];
  snprintf(bid_prefix,8*2+1,"%02x%02x%02x%02x%02x%02x%02x%02x",
	   msg[offset+0],msg[offset+1],msg[offset+2],msg[offset+3],
	   msg[offset+4],msg[offset+5],msg[offset+6],msg[offset+7]);
  offset+=8;
  long long version=0;
  for(int i=0;i<8;i++) version|=((long long)msg[offset+i])<<(i*8LL);
  offset+=8;
  long long offset_compound=0;
  for(int i=0;i<4;i++) offset_compound|=((long long)msg[offset+i])<<(i*8LL);
  offset+=4;

  if (monitor_mode)
    {
      char sender_prefix[128];
      char monitor_log_buf[1024];
      sprintf(sender_prefix,"%s*",sender->sid_prefix);
      char bid_prefix[128];
      bytes_to_prefix(&msg[bid_prefix_offset],bid_prefix);
      snprintf(monitor_log_buf,sizeof(monitor_log_buf),
	       "Payload length: BID=%s*, version 0x%010llx, length = %lld bytes",
	       bid_prefix,version,offset_compound);
      
      monitor_log(sender_prefix,NULL,monitor_log_buf);
    }
  
  saw_length(sender_prefix,bid_prefix,version,offset_compound);
  
  return offset;
}

