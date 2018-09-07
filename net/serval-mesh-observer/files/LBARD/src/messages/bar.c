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

#ifdef SYNC_BY_BAR
int append_bar(int bundle_number,int *offset,int mtu,unsigned char *msg_out)
{
  // BAR consists of:
  // 8 bytes : BID prefix
  // 8 bytes : version
  // 4 bytes : recipient prefix
  // 1 byte : log2(ish) size and meshms flag

  msg_out[(*offset)++]='B'; // indicates a BAR follows
  
  for(int i=0;i<8;i++)
    msg_out[(*offset)++]=hex_byte_value(&bundles[bundle_number].bid[i*2]);
  for(int i=0;i<8;i++)
    msg_out[(*offset)++]=(bundles[bundle_number].version>>(i*8))&0xff;
  for(int i=0;i<4;i++)
    msg_out[(*offset)++]=hex_byte_value(&bundles[bundle_number].recipient[i*2]);
  int size_byte=log2ish(bundles[bundle_number].length);
  if ((!strcasecmp("MeshMS1",bundles[bundle_number].service))
      ||(!strcasecmp("MeshMS2",bundles[bundle_number].service)))
    size_byte&=0x7f;
  else
    size_byte|=0x80;
  msg_out[(*offset)++]=size_byte;

  char status_msg[1024];
  snprintf(status_msg,1024,"Announcing BAR %c%c%c%c%c%c%c%c* version %lld [%s]",
	   bundles[bundle_number].bid[0],bundles[bundle_number].bid[1],
	   bundles[bundle_number].bid[2],bundles[bundle_number].bid[3],
	   bundles[bundle_number].bid[4],bundles[bundle_number].bid[5],
	   bundles[bundle_number].bid[6],bundles[bundle_number].bid[7],
	   bundles[bundle_number].version,	   
	   bundles[bundle_number].service);
  status_log(status_msg);

  
  return 0;
}
#endif

int sync_build_bar_in_slot(int slot,unsigned char *bid_bin,
			   long long bundle_version)
{
  int ofs=0;
  report_queue[slot][ofs++]='B';

  // BID prefix
  for(int i=0;i<8;i++) report_queue[slot][ofs++]=bid_bin[i];
  // Bundle Version
  for(int i=0;i<8;i++) report_queue[slot][ofs++]=(bundle_version>>(i*8))&0xff;
  // Dummy recipient + size byte
  for(int i=0;i<5;i++) report_queue[slot][ofs++]=(bundle_version>>(i*8))&0xff;

  report_lengths[slot]=ofs;
  assert(ofs<MAX_REPORT_LEN);
  return 0;
}

int message_parser_42(struct peer_state *sender,unsigned char *prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  int offset=0;
  if (length-offset<BAR_LENGTH) {
    fprintf(stderr,"Ignoring runt BAR (len=%d instead of %d)\n",
	    length-offset,BAR_LENGTH);
    return -2;
  }
  offset++;
  // BAR announcement
  unsigned char *bid_prefix_bin=&msg[offset];
  char bid_prefix[8*2+1+1];
  snprintf(bid_prefix,8*2+1,"%02X%02X%02X%02X%02X%02X%02X%02X",
	   msg[offset+0],msg[offset+1],msg[offset+2],msg[offset+3],
	   msg[offset+4],msg[offset+5],msg[offset+6],msg[offset+7]);
  offset+=8;
  long long version=0;
  for(int i=0;i<8;i++) version|=((long long)msg[offset+i])<<(i*8LL);
  offset+=8;
  char recipient_prefix[4*2+1+1];
  snprintf(recipient_prefix,4*2+1,"%02x%02x%02x%02x",
	   msg[offset+0],msg[offset+1],msg[offset+2],msg[offset+3]);
  offset+=4;
  unsigned char size_byte=msg[offset];
  offset+=1;
#ifdef SYNC_BY_BAR
  if (debug_pieces)
    printf(
	   "Saw a BAR from %s*: %s* version %lld size byte 0x%02x"
	   " (we know of %d bundles held by that peer)\n",
	   sender->sid_prefix,bid_prefix,version,size_byte,sender->bundle_count);
#endif
  if (monitor_mode)
    {
      char sender_prefix[128];
      char monitor_log_buf[1024];
      sprintf(sender_prefix,"%s*",sender->sid_prefix);
      snprintf(monitor_log_buf,sizeof(monitor_log_buf),
	       "BAR: BID=%s*, version 0x%010llx,"
	       " %smeshms payload has %lld--%lld bytes,"
#ifdef SYNC_BY_BAR
	       " (%d unique)"
#endif
	       ,
	       bid_prefix,version,
	       (size_byte&0x80)?"non-":"",
	       (size_byte&0x7f)?(size_byte_to_length((size_byte&0x7f)-1)):0,
	       size_byte_to_length((size_byte&0x7f))-1
#ifdef SYNC_BY_BAR
	       ,sender->bundle_count
#endif
	       );	
      
      monitor_log(sender_prefix,NULL,monitor_log_buf);
    }
  
#ifdef SYNC_BY_BAR
  peer_note_bar(sender,bid_prefix,version,recipient_prefix,size_byte);
#else
  int bundle=lookup_bundle_by_prefix_bin_and_version_or_older(bid_prefix_bin,version);
  if (bundle>-1) {
    printf("T+%lldms : SYNC FIN: %s* has finished receiving"
	   " %s version %lld (bundle #%d)\n",
	   gettime_ms()-start_time,sender?sender->sid_prefix:"<null>",bid_prefix,
	   version,bundle);
    
    sync_dequeue_bundle(sender,bundle);
  } else {
    printf("T+%lldms : SYNC FIN: %s* has finished receiving"
	   " %s (%02X...) version %lld (NO SUCH BUNDLE!)\n",
	   gettime_ms()-start_time,sender?sender->sid_prefix:"<null>",
	   bid_prefix,bid_prefix_bin[0],version);
  }
  
#endif
  
  return offset;
}

