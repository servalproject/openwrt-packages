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

int request_segment(int peer, char *bid_prefix, int bundle_length,
		    int seg_start, int is_manifest,
		    int *offset, int mtu,unsigned char *msg_out)
{
  // Check that we have enough space
  if ((mtu-*offset)<(1+2+8+3)) return -1;

  int start_offset=*offset;
  
  // Request piece
  msg_out[(*offset)++]='R';

  // First 2 bytes only of peer SID. This will almost always get the benefit of
  // specifying the peer precisely, but save bytes on average
  msg_out[(*offset)++]=peer_records[peer]->sid_prefix_bin[0];
  msg_out[(*offset)++]=peer_records[peer]->sid_prefix_bin[1];

  // BID prefix
  for(int i=0;i<8;i++)
    msg_out[(*offset)++]=
      hex_to_val(bid_prefix[i*2+1])
      +hex_to_val(bid_prefix[i*2+0])*16;

  // Start offset of interest
  msg_out[(*offset)++]=(seg_start>>0)&0xff;
  msg_out[(*offset)++]=(seg_start>>8)&0xff;
  msg_out[(*offset)++]=((seg_start>>16)&0x7f)|(is_manifest?0x80:0x00);

  if (debug_pull) {
    printf("Requesting BID=%s @ %c%d (len=%d) from SID=%s*\n",
	    bid_prefix,
	    is_manifest?'M':'B',seg_start,
	    bundle_length,
	    peer_records[peer]->sid_prefix);
    printf("Request block: ");
    for(;start_offset<*offset;start_offset++)
      printf(" %02X",msg_out[start_offset]);
    printf("\n");
  }

  char status_msg[1024];
  snprintf(status_msg,1024,"Requesting BID=%s @ %c%d (len=%d) from SID=%s*\n",
	    bid_prefix,
	    is_manifest?'M':'B',seg_start,
	    bundle_length,
	    peer_records[peer]->sid_prefix);
  status_log(status_msg);
  
  return 0;
}

int message_parser_52(struct peer_state *sender,char *sender_prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  int offset=0;
  // Request for a segment
  {
    char target_sid[4+1+1];
    char bid_prefix[8*2+1+1];
    int bundle_offset=0;
    int is_manifest=0;
    offset++;
    snprintf(target_sid,5,"%02x%02x",msg[offset],msg[offset+1]);
    offset+=2;
    snprintf(bid_prefix,17,"%02x%02x%02x%02x%02x%02x%02x%02x",
	     msg[offset+0],msg[offset+1],msg[offset+2],msg[offset+3],
	     msg[offset+4],msg[offset+5],msg[offset+6],msg[offset+7]);
    offset+=8;
    bundle_offset|=msg[offset++];
    bundle_offset|=msg[offset++]<<8;
    bundle_offset|=msg[offset++]<<16;
    // We can only request segments upto 8MB point in a bundle via this transport!
    // XXX here be dragons
    if (bundle_offset&0x800000) is_manifest=1;
    bundle_offset&=0x7fffff;
    
    if (debug_pull) {
      printf("Saw request from SID=%s* BID=%s @ %c%d addressed to SID=%s*\n",
	     sender_prefix,bid_prefix,is_manifest?'M':'B',bundle_offset,
	     target_sid);
    }
    {
      char status_msg[1024];
      snprintf(status_msg,1024,"Saw request from SID=%s* BID=%s @ %c%d addressed to SID=%s*\n",
	       sender_prefix,bid_prefix,is_manifest?'M':'B',bundle_offset,
	       target_sid);
      status_log(status_msg);
    }
    
    if (monitor_mode)
      {
	char sender_prefix[128];
	char monitor_log_buf[1024];
	sprintf(sender_prefix,"%s*",sender->sid_prefix);
	snprintf(monitor_log_buf,sizeof(monitor_log_buf),
		 "Request for BID=%s*, beginning at offset %d of %s.",
		 bid_prefix,bundle_offset,is_manifest?"manifest":"payload");
	
	monitor_log(sender_prefix,NULL,monitor_log_buf);
      }
    
#ifdef SYNC_BY_BAR
    // Are we the target SID?
    if (!strncasecmp(my_sid,target_sid,4)) {
      if (debug_pull) printf("  -> request is for us.\n");
      // Yes, it is addressed to us.
      // See if we have this bundle, and if so, set the appropriate stream offset
      // and mark the bundle as requested
      // XXX linear search!
      for(int i=0;i<bundle_count;i++) {
	if (!strncasecmp(bid_prefix,bundles[i].bid,16)) {
	  if (debug_pull) printf("  -> found the bundle.\n");
	  bundles[i].transmit_now=time(0)+TRANSMIT_NOW_TIMEOUT;
	  if (debug_announce) {
	    printf("*** Setting transmit_now flag on %s*\n",
		   bundles[i].bid);
	  }
	  
	  // When adjusting the offset, don't adjust it if we are going to reach
	  // that point within a few hundred bytes, as it won't save any time, and
	  // it might just cause confusion and delay because of the latency of us
	  // receiving the message and responding to it.
	  if (is_manifest) {
	    if ((bundle_offset<bundles[i].last_manifest_offset_announced)
		||((bundle_offset-bundles[i].last_manifest_offset_announced)>500)) {
	      bundles[i].last_manifest_offset_announced=bundle_offset;
	      if (debug_pull) printf("  -> setting manifest announcement offset to %d.\n",bundle_offset);
	    }
	  } else {
	    if ((bundle_offset<bundles[i].last_offset_announced)
		||((bundle_offset-bundles[i].last_offset_announced)>500)) {
	      bundles[i].last_offset_announced=bundle_offset;
	      if (debug_pull) printf("  -> setting body announcement offset to %d.\n",bundle_offset);
	    }
	  }
	}
      }
    }
#endif
  }
  
  return offset;
}

