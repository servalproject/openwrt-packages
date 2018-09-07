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

int sync_schedule_progress_report(int peer, int partial, int randomJump)
{
  int slot=report_queue_length;

  for(int i=0;i<report_queue_length;i++) {
    if (report_queue_peers[i]==peer_records[peer]) {
      // We already want to tell this peer something.
      // We should only need to tell a peer one thing at a time.
      slot=i; break;
    }
  }

  // Work out where we will request data to be sent from
  int isReallyFirstByte=0;
  int first_required_body_offset
    =partial_find_missing_byte(partials[partial].body_segments,&isReallyFirstByte);
  
  if (slot>=REPORT_QUEUE_LEN) slot=random()%REPORT_QUEUE_LEN;

  // Mark utilisation of slot, so that we can flush out stale messages
  report_queue_partials[slot]=partial;
  report_queue_peers[slot]=peer_records[peer];
  
  int ofs=0;

  // Differentiate between an ACK which is really from the earliest byte we could need,
  // versus from the start of a region we are still missing
  // (Senders can use this information to keep bitmap state from the earliest missing
  // byte, if we ack from a later missing region, but they are close enough together
  // that it makes sense to keep from the start. This is especially useful for bundles
  // that are smaller than the 16KB bitmap window, where otherwise useful information
  // is being discarded, which could result in unnecessary retransmission.
  if (isReallyFirstByte) {
    // We are really requesting the first byte we could ever need
    if (randomJump) report_queue[slot][ofs++]='f';
    else report_queue[slot][ofs++]='F';
  } else {
    // We are requeting the first byte of of a region we need, but it is not the first
    if (randomJump) report_queue[slot][ofs++]='a';
    else report_queue[slot][ofs++]='A';
  }

  if (report_queue_message[slot]) {
    fprintf(stderr,"Replacing report_queue message '%s' with 'progress report' (ACK)\n",
	    slot>=0?(report_queue_message[slot]?report_queue_message[slot]:"< report_queue_message=slot >"):"< slot<0 >");
    free(report_queue_message[slot]);
    report_queue_message[slot]=NULL;
  } else {
    fprintf(stderr,"Setting report_queue message to 'progress report' (ACK)\n");
  }
  report_queue_message[slot]=strdup("progress report");

  // BID prefix
  for(int i=0;i<8;i++) {
    int hex_value=0;
    char hex_string[3]={partials[partial].bid_prefix[i*2+0],
			partials[partial].bid_prefix[i*2+1],
			0};
    hex_value=strtoll(hex_string,NULL,16);
    report_queue[slot][ofs++]=hex_value;
  }
  
  // manifest and body offset
  // (for manifest, it can only consist of 16 x 64 byte pieces, so instead
  //  always send a bitmap for the manifest progress. But we also calculate
  //  the first offset for debug purposes.)
  int first_required_manifest_offset=1024;
  for(int i=0;i<16;i++)
    if (!(partials[partial].request_manifest_bitmap[i>>3]&(1<<(i&7))))
      { first_required_manifest_offset=i*64; break; }

  report_queue[slot][ofs++]=partials[partial].request_manifest_bitmap[0];
  report_queue[slot][ofs++]=partials[partial].request_manifest_bitmap[1];
  report_queue[slot][ofs++]=first_required_body_offset&0xff;
  report_queue[slot][ofs++]=(first_required_body_offset>>8)&0xff;
  report_queue[slot][ofs++]=(first_required_body_offset>>16)&0xff;
  report_queue[slot][ofs++]=(first_required_body_offset>>24)&0xff;

  // Include who we are asking
  report_queue[slot][ofs++]=peer_records[peer]->sid_prefix_bin[0];
  report_queue[slot][ofs++]=peer_records[peer]->sid_prefix_bin[1];
  
  report_lengths[slot]=ofs;
  assert(ofs<MAX_REPORT_LEN);
  if (slot>=report_queue_length) report_queue_length=slot+1;

  if (randomJump) {
    if (!monitor_mode)
      fprintf(stderr,
	      "T+%lldms : Redirecting %s to an area we have not yet received of %s*/%lld, i.e., somewhere not before m_first=%d, b_first=%d\n",
	      gettime_ms()-start_time,
	      peer_records[peer]->sid_prefix,
	      partials[partial].bid_prefix,
	      partials[partial].bundle_version,
	      first_required_manifest_offset,
	      first_required_body_offset);    
  } else
      if (!monitor_mode)
	fprintf(stderr,
		"T+%lldms : ACKing progress on transfer of %s* from %s. m_first=%d, b_first=%d\n",
		gettime_ms()-start_time,
		partials[partial].bid_prefix,
		peer_records[peer]->sid_prefix,
		first_required_manifest_offset,
		first_required_body_offset);    
  
  return 0;
}

int sync_parse_ack(struct peer_state *p,unsigned char *msg,
		   char *sid_prefix_hex,
		   char *servald_server, char *credential)
{
  // Get fields

  // Manifest progress is now exclusively sent as a bitmap, not an offset.
  // Compute first useful offset for legacy purposes.
  int manifest_offset=1024;
  for(int i=0;i<16;i++) if (!(msg[9+(i>>3)]&(1<<(i&7)))) { manifest_offset=i*64; break; }

  int body_offset=msg[11]|(msg[12]<<8)|(msg[13]<<16)|(msg[14]<<24);
  int for_me=0;

  if ((msg[15]==my_sid[0])&&(msg[16]==my_sid[1])) for_me=1;
  
  // Does the ACK tell us to jump exactly here, or to a random place somewhere
  // after it?  If it indicates a random jump, only do the jump 1/2 the time.
  int randomJump=0;
  if(msg[0]=='a'||msg[0]=='f') randomJump=random()&1;

  unsigned char *bid_prefix=&msg[1];

  int bundle=lookup_bundle_by_prefix(bid_prefix,8);

  fprintf(stderr,"T+%lldms : SYNC ACK: '%c' %s* is asking for %s (%02X%02X) to send from m=%d, p=%d of"
	  " %02x%02x%02x%02x%02x%02x%02x%02x (bundle #%d/%d)\n",
	  gettime_ms()-start_time,
	  msg[0],
	  p?p->sid_prefix:"<null>",
	  for_me?"us":"someone else",
	  msg[15],msg[16],
	  manifest_offset,body_offset,
	  msg[1],msg[2],msg[3],msg[4],msg[5],msg[6],msg[7],msg[8],
	  bundle,bundle_count);

  if (!for_me) return 0;
  
  // Sanity check inputs, so that we don't mishandle memory.
  if (manifest_offset<0) manifest_offset=0;
  if (body_offset<0) body_offset=0;  

  if (bundle<0) return -1;

  if (bundle==p->request_bitmap_bundle) {

    // For manifest progress, simply copy in the manifest progress bitmap
    p->request_manifest_bitmap[0]=msg[9];
    p->request_manifest_bitmap[0]=msg[10];

    // Reset (or translate) TX bitmap, since we are being asked to send from here.
    if (msg[0]=='F'&&msg[0]=='f') {
      // Message types  F and f indicate that this really is the first byte we
      // could ever need, so translate the progress bitmap to the new offset
      progress_bitmap_translate(p,body_offset);
    } else {
      // Message types A and a indicate that there are lowered number byte(s) we
      // still need, so we should only conservatively advance the bitmap, so as
      // to keep as much of the state that we have that might tell us which those
      // missing bytes might be.
      int body_offset_conservative=p->request_bitmap_offset;
      // First, always go backwards if we need to
      if (body_offset<body_offset_conservative) body_offset_conservative=body_offset;
      // And advance only if the new offset would be <4KB from the end of the window
      // (and only then if we aren't sure that the bundle is <= 16KB)
      if (bundles[bundle].length>(16*1024)) {
	if (body_offset>(body_offset_conservative+(12*1024)))
	  body_offset_conservative=body_offset;	    
      }
    }
  }
  
  if (bundle==p->tx_bundle) {

    if (!(option_flags&FLAG_NO_HARD_LOWER)) {
      p->tx_bundle_manifest_offset_hard_lower_bound=manifest_offset;
      p->tx_bundle_body_offset_hard_lower_bound=body_offset;
      if (debug_ack)
	fprintf(stderr,"HARDLOWER: Setting hard lower limit to M/B = %d/%d due to ACK packet\n",
		manifest_offset,body_offset);
    }
    if (randomJump) {
      // Jump to a random position somewhere after the provided points.
      if (!prime_bundle_cache(bundle,
			      sid_prefix_hex,servald_server,credential))
	{
	  if (manifest_offset<cached_manifest_encoded_len) {
	    if (!(option_flags&FLAG_NO_RANDOMIZE_REDIRECT_OFFSET)) {
	      if (cached_manifest_encoded_len-manifest_offset)
		manifest_offset+=random()%(cached_manifest_encoded_len-manifest_offset);
	      manifest_offset&=0xffffff00;
	    }
	  }
	  if (body_offset<cached_body_len) {
	    if (!(option_flags&FLAG_NO_RANDOMIZE_REDIRECT_OFFSET)) {
	      if (cached_body_len-body_offset)
		body_offset+=random()%(cached_body_len-body_offset);
	      body_offset&=0xffffff00;
	    }
	  }
	  fprintf(stderr,"SYNC ACK: %s* is redirected us to a random location. Sending from m=%d, p=%d\n",
		  p->sid_prefix,manifest_offset,body_offset);
	}
    } else 
      fprintf(stderr,"SYNC ACK: %s* is asking for us to send from m=%d, p=%d\n",
	      p->sid_prefix,manifest_offset,body_offset);
    p->tx_bundle_manifest_offset=manifest_offset;
    p->tx_bundle_body_offset=body_offset;      
  } else {
    fprintf(stderr,"SYNC ACK: Ignoring, because we are sending bundle #%d, and request is for bundle #%d\n",p->tx_bundle,bundle);
    fprintf(stderr,"          Requested BID/version = %s/%lld\n",
	    bundles[bundle].bid_hex, bundles[bundle].version);
    fprintf(stderr,"                 TX BID/version = %s/%lld\n",
	    bundles[p->tx_bundle].bid_hex, bundles[p->tx_bundle].version);
  }

  return 0;
}

#define message_parser_46 message_parser_41
#define message_parser_66 message_parser_41
#define message_parser_61 message_parser_41

int message_parser_41(struct peer_state *sender,char *prefix,
		      char *servald_server, char *credential,
		      unsigned char *message,int length)
{
  sync_parse_ack(sender,message,prefix,servald_server,credential);
  return 17;
}

