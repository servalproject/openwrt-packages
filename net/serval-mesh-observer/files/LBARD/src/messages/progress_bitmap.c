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

int sync_schedule_progress_report_bitmap(int peer, int partial)
{
  printf(">>> %s Scheduling bitmap report.\n",timestamp_str());

  // find first required body offset

  int slot=report_queue_length;

  for(int i=0;i<report_queue_length;i++) {
    // BITMAP reports are broadcast, so not per-peer
    //    if (report_queue_peers[i]==peer_records[peer]) {
      // We already want to tell this peer something.
      // We should only need to tell a peer one thing at a time.
      slot=i; break;
      //    }
  }
  
  if (slot>=REPORT_QUEUE_LEN) slot=random()%REPORT_QUEUE_LEN;

  // Mark utilisation of slot, so that we can flush out stale messages
  report_queue_partials[slot]=partial;
  report_queue_peers[slot]=peer_records[peer];

  int ofs=0;

  if (!monitor_mode) {
    if (report_queue_message[slot]) {
      fprintf(stderr,"Replacing report_queue message '%s' with 'progress report' (BITMAP)\n",
	      slot>=0?(report_queue_message[slot]?report_queue_message[slot]:"< report_queue_message=slot >"):"< slot<0 >");
      free(report_queue_message[slot]);
      report_queue_message[slot]=NULL;
    } else {
      fprintf(stderr,"Setting report_queue message to 'progress report' (BITMAP)\n");
    }
  }
  report_queue_message[slot]=strdup("progress report");
  
  // Announce progress bitmap to all recipients.
  partial_update_request_bitmap(&partials[partial]);
  report_queue[slot][ofs++]='M';
  
  // BID prefix
  for(int i=0;i<8;i++) {
    int hex_value=0;
    char hex_string[3]={partials[partial].bid_prefix[i*2+0],
			partials[partial].bid_prefix[i*2+1],
			0};
    hex_value=strtoll(hex_string,NULL,16);
    report_queue[slot][ofs++]=hex_value;
  }
  
  // Current manifest reception state (16 bits is all we ever need)
  report_queue[slot][ofs++]=partials[partial].request_manifest_bitmap[0];
  report_queue[slot][ofs++]=partials[partial].request_manifest_bitmap[1];
  
  // Start of region of interest
  for(int i=0;i<4;i++)
    report_queue[slot][ofs++]
      =(partials[partial].request_bitmap_start>>(i*8))&0xff;
  
  // 32 bytes of bitmap
  for(int i=0;i<32;i++)
    report_queue[slot][ofs++]=partials[partial].request_bitmap[i];

  report_lengths[slot]=ofs;
  assert(ofs<MAX_REPORT_LEN);
  if (slot>=report_queue_length) report_queue_length=slot+1;

  return 0;
}

int sync_parse_progress_bitmap(struct peer_state *p,unsigned char *msg_in,int *offset)
{  
  unsigned char *msg=&msg_in[*offset];
  (*offset)+=1; // Skip 'M'
  (*offset)+=8; // Skip BID prefix
  (*offset)+=2; // Skip manifest bitmap
  (*offset)+=4; // Skip start of region of interest
  (*offset)+=32; // Skip progress bitmap

  // Get fields
  unsigned char *bid_prefix=&msg[1];
  unsigned char *manifest_bitmap=&msg[9];
  int body_offset=msg[11]|(msg[12]<<8)|(msg[13]<<16)|(msg[14]<<24);
  unsigned char *bitmap=&msg[15];
  int bundle=lookup_bundle_by_prefix(bid_prefix,8);
  int manifest_offset=1024;    
  
  if (p->tx_bundle==bundle) {
    // We are sending this bundle to them, so update our info

    // XXX - We should also remember these as the last verified progress,
    // so that when we fill the bitmap, we can resend all not yet-acknowledged content

    p->request_bitmap_bundle=bundle;
    p->request_bitmap_offset=body_offset;
    memcpy(p->request_bitmap,bitmap,32);

    // Update manifest bitmap ...
    memcpy(p->request_manifest_bitmap,manifest_bitmap,2);
    // ... and quickly recalculate first useful TX point
    for(int i=0;i<16;i++) if (!(manifest_bitmap[i>>3]&(1<<(i&7)))) { manifest_offset=i*64; break; }
    p->tx_bundle_manifest_offset=manifest_offset;
  }

  if (debug_bitmap)
    printf(">>> %s BITMAP ACK: %s* is informing everyone to send from m=%d (%02x%02x), p=%d of"
	   " %02x%02x%02x%02x%02x%02x%02x%02x (bundle #%d/%d):  ",
	   timestamp_str(),
	   p?p->sid_prefix:"<null>",
	   manifest_offset,
	   p->request_manifest_bitmap[0],p->request_manifest_bitmap[1],
	   body_offset,
	   msg[1],msg[2],msg[3],msg[4],msg[5],msg[6],msg[7],msg[8],
	   bundle,bundle_count);

  int max_block=256;
  if (bundle>-1) {
    max_block=(bundles[bundle].length-p->request_bitmap_offset);
    if (max_block&0x3f)
      max_block=1+max_block/64;
    else
      max_block=0+max_block/64;    
  }
  if (max_block>256) max_block=256;
  if (debug_bitmap) dump_progress_bitmap(stdout, bitmap, max_block);
  
  return 0;
}

int message_parser_4D(struct peer_state *sender,char *sender_prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  int offset=0;
  sync_parse_progress_bitmap(sender,msg,&offset);
  
  return offset;
}

