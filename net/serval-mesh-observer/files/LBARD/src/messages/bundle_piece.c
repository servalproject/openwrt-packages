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

int sync_append_some_bundle_bytes(int bundle_number,int start_offset,int len,
				  unsigned char *p, int is_manifest,
				  int *offset,int mtu,unsigned char *msg,
				  int target_peer)
{
  int max_bytes=mtu-(*offset)-21;
  int bytes_available=len-start_offset;
  int actual_bytes=0;
  int not_end_of_item=0;

  // If we can't announce even one byte, we should just give up.
  if (start_offset>0xfffff) {
    max_bytes-=2; if (max_bytes<0) max_bytes=0;
  }
  if (max_bytes<1) return -1;

  // Work out number of bytes to include in announcement
  if (bytes_available<max_bytes) {
    actual_bytes=bytes_available;
    not_end_of_item=0;
  } else {
    actual_bytes=max_bytes;
    not_end_of_item=1;
  }

  // If not sending last piece, limit to 64 byte segment boundary.
  // This is partly to aid debugging, but also avoids wasting bytes when we are
  // using the request bitmap for transfers, where accounting is in 64 byte units.
  if (not_end_of_item) {
    int end_point=start_offset+actual_bytes;
    if (!(option_flags&FLAG_NO_BITMAP_PROGRESS)) {
      if (end_point&63) actual_bytes-=end_point&63;
    }
    if (actual_bytes<1) return 0;
  }
  
  // Make sure byte count fits in 11 bits.
  if (actual_bytes>0x7ff) actual_bytes=0x7ff;

  if (actual_bytes<0) return -1;

  printf(">>> %s I just sent %s piece [%d,%d) for %s*.\n",
	 timestamp_str(),is_manifest?"manifest":"body",
	 start_offset,start_offset+actual_bytes,
	 peer_records[target_peer]->sid_prefix);
  peer_update_request_bitmaps_due_to_transmitted_piece(bundle_number,is_manifest,
						       start_offset,actual_bytes);
  dump_peer_tx_bitmap(target_peer);
  
  // Generate 4 byte offset block (and option 2-byte extension for big bundles)
  long long offset_compound=0;
  offset_compound=(start_offset&0xfffff);
  offset_compound|=((actual_bytes&0x7ff)<<20);
  if (is_manifest) offset_compound|=0x80000000;
  offset_compound|=((start_offset>>20LL)&0xffffLL)<<32LL;

  // Now write the 23/25 byte header and actual bytes into output message
  // BID prefix (8 bytes)
  if (start_offset>0xfffff)
    msg[(*offset)++]='P'+not_end_of_item;
  else 
    msg[(*offset)++]='p'+not_end_of_item;

  // Intended recipient
  msg[(*offset)++]=peer_records[target_peer]->sid_prefix_bin[0];
  msg[(*offset)++]=peer_records[target_peer]->sid_prefix_bin[1];
  
  for(int i=0;i<8;i++) msg[(*offset)++]=bundles[bundle_number].bid_bin[i];
  // Bundle version (8 bytes)
  for(int i=0;i<8;i++)
    msg[(*offset)++]=(cached_version>>(i*8))&0xff;
  // offset_compound (4 bytes)
  for(int i=0;i<4;i++)
    msg[(*offset)++]=(offset_compound>>(i*8))&0xff;
  if (start_offset>0xfffff) {
    for(int i=4;i<6;i++)
      msg[(*offset)++]=(offset_compound>>(i*8))&0xff;
  }

  bcopy(p,&msg[(*offset)],actual_bytes);
  (*offset)+=actual_bytes;

  /* Advance the cursor for sending this bundle to all other peers if their cursor
     sits within the window we have just sent. */
  for(int pn=0;pn<peer_count;pn++) {
    if (pn!=target_peer&&peer_records[pn]) {
      if (peer_records[pn]->tx_bundle==bundle_number) {
	if (is_manifest) {
	  if ((peer_records[pn]->tx_bundle_manifest_offset>=start_offset)
	      &&(peer_records[pn]->tx_bundle_manifest_offset<(start_offset+actual_bytes)))
	    peer_records[pn]->tx_bundle_manifest_offset=(start_offset+actual_bytes);
	} else {
	  if ((peer_records[pn]->tx_bundle_body_offset>=start_offset)
	      &&(peer_records[pn]->tx_bundle_body_offset<(start_offset+actual_bytes))) {
	    if (debug_pieces)
	      printf(">>> %s Cursor advance from %d to %d, due to sending [%d..%d].\n",
		     timestamp_str(),
		     peer_records[pn]->tx_bundle_body_offset,(start_offset+actual_bytes),
		     start_offset,(start_offset+actual_bytes)
		     );
	    peer_records[pn]->tx_bundle_body_offset=(start_offset+actual_bytes);
	  }
	}
      }
    }
  }
  
  if (debug_announce) {
    printf("T+%lldms : Announcing for %s* ",gettime_ms()-start_time,
	   peer_records[target_peer]->sid_prefix);
    for(int i=0;i<8;i++) printf("%c",bundles[bundle_number].bid_hex[i]);
    printf("* (priority=0x%llx) version %lld %s segment [%d,%d)\n",
	   bundles[bundle_number].last_priority,
	   bundles[bundle_number].version,
	   is_manifest?"manifest":"payload",
	   start_offset,start_offset+actual_bytes);
  }

  char status_msg[1024];
  snprintf(status_msg,1024,"Announcing %c%c%c%c%c%c%c%c* version %lld %s segment [%d,%d)",
	   bundles[bundle_number].bid_hex[0],bundles[bundle_number].bid_hex[1],
	   bundles[bundle_number].bid_hex[2],bundles[bundle_number].bid_hex[3],
	   bundles[bundle_number].bid_hex[4],bundles[bundle_number].bid_hex[5],
	   bundles[bundle_number].bid_hex[6],bundles[bundle_number].bid_hex[7],
	   bundles[bundle_number].version,
	   is_manifest?"manifest":"payload",
	   start_offset,start_offset+actual_bytes);
  status_log(status_msg);

  return actual_bytes;
}

int saw_piece(char *peer_prefix,int for_me,
	      char *bid_prefix, unsigned char *bid_prefix_bin,
	      long long version,
	      long long piece_offset,int piece_bytes,int is_end_piece,
	      int is_manifest_piece,unsigned char *piece,

	      char *prefix, char *servald_server, char *credential)
{
  int next_byte_would_be_useful=0;
  int new_bytes_in_piece=0;
  
  int peer=find_peer_by_prefix(peer_prefix);
  if (peer<0) {
    printf(">>> %s Saw a piece from unknown SID=%s* -- ignoring.\n",
	 timestamp_str(),peer_prefix);
    return -1;
  }

  if (debug_pieces)
  printf(">>> %s Saw a piece of BID=%s* from SID=%s*: %s [%lld,%lld) %s\n",
	 timestamp_str(),bid_prefix,peer_prefix,
	 is_manifest_piece?"manifest":"body",
	 piece_offset,piece_offset+piece_bytes,
	 is_end_piece?"END PIECE":"");
  
  int bundle_number=-1;

  // Send an ack immediately if we already have this bundle (or newer), so that the
  // sender knows that they can start sending something else.
  // This in effect provides a positive ACK for reception of a new bundle.
  // XXX - If the sender depends on the ACK to start sending the next bundle, then
  // an adversary could purposely refuse to acknowledge bundles (that it might have
  // introduced for this special purpose) addressed to itself, so that the priority
  // scheme gets stuck trying to send these bundles to them forever.
  if (for_me) {
    if (sync_is_bundle_recently_received(bid_prefix,version)) {
      // We have this version already: mark it for announcement to sender,
      // and then return immediately.
      fprintf(stderr,
	      "We recently received %s* version %lld - ignoring piece.\n",
	      bid_prefix,version);
      sync_tell_peer_we_have_bundle_by_id(peer,bid_prefix_bin,version);
      return 0;      
    }
  }
  for(int i=0;i<bundle_count;i++) {
    if (!strncasecmp(bid_prefix,bundles[i].bid_hex,strlen(bid_prefix))) {
      if (debug_pieces) printf("We have version %lld of BID=%s*.  %s is offering %s version %lld\n",
			       bundles[i].version,bid_prefix,peer_prefix,for_me?"us":"someone else",version);
      if (version<=bundles[i].version) {
	// We have this version already: mark it for announcement to sender,
	// and then return immediately.
#ifdef SYNC_BY_BAR
	bundles[i].announce_bar_now=1;
#endif
	if (for_me) {
	  fprintf(stderr,"We already have %s* version %lld - ignoring piece.\n",
		  bid_prefix,version);
	  sync_tell_peer_we_have_this_bundle(peer,i);
	}

	// Even if it wasn't addressed to us, we now know that this peer doesn't have the bundle.
	sync_queue_bundle(peer_records[peer],i);
	
	// Update progress bitmaps for all peers whenver we see a piece received that we
	// think that they might want.  This stops us from resending the same piece later.
	if (bundle_number>=0) {
	  printf(">>> %s Examining transmitted piece for bitmap updates.\n",
		 timestamp_str());
	  peer_update_request_bitmaps_due_to_transmitted_piece(bundle_number,is_manifest_piece,
							       piece_offset,piece_bytes);
	}
	
	return 0;
      } else {
	// We have an older version.
	// Remember the bundle number so that we can pre-fetch the body we have
	// for incremental journal transfers
	if (version<0x100000000LL) {
	  bundle_number=i;
	}	
      }
    }
  }

  // Update progress bitmaps for all peers whenver we see a piece received that we
  // think that they might want.  This stops us from resending the same piece later.
  if (bundle_number>=0) {
    printf(">>> %s Examining transmitted piece for bitmap updates.\n",
	   timestamp_str());
    peer_update_request_bitmaps_due_to_transmitted_piece(bundle_number,is_manifest_piece,
							 piece_offset,piece_bytes);
  }
  
  int i;
  int spare_record=random()%MAX_BUNDLES_IN_FLIGHT;
  for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
    if (!partials[i].bid_prefix) {
      if (spare_record==-1) spare_record=i;
    } else {
      if (!strcasecmp(partials[i].bid_prefix,bid_prefix))
	{
	  if (debug_pieces) printf("Saw another piece for BID=%s* from SID=%s: ",
			 bid_prefix,peer_prefix);
	  if (debug_pieces) printf("[%lld..%lld)\n",
			 piece_offset,piece_offset+piece_bytes);

	  break;
	}
      else {
	if (debug_pieces) {
	  printf("  this isn't the partial we are looking for.\n");
	  printf("  piece is of %s*, but slot #%d has %s*\n",
		 bid_prefix,i,
		 partials[i].bid_prefix);
	}
      }
    }
  }

  if (debug_pieces)
    printf("Saw a piece of interesting bundle BID=%s*/%lld from SID=%s\n",
	    bid_prefix,version, peer_prefix);
  
  if (i==MAX_BUNDLES_IN_FLIGHT) {
    if (spare_record>0) i=spare_record;
    if (debug_pieces)
      printf("Didn't find bundle in partials for this peer. first spare slot =%d\n",spare_record);
    // Didn't find bundle in the progress list.
    // Abort one of the ones in the list at random, and replace, unless there is
    // a spare record slot to use.
    if (spare_record==-1) {
      i=random()%MAX_BUNDLES_IN_FLIGHT;
      clear_partial(&partials[i]);
    } else {
      i=spare_record;
      // Clear it just to make sure.
      clear_partial(&partials[i]);
    }
    if (debug_pieces)
      printf("@@@   Using slot %d\n",i);

    // Now prepare the partial record
    partials[i].bid_prefix=strdup(bid_prefix);
    partials[i].bundle_version=version;
    partials[i].manifest_length=-1;
    partials[i].body_length=-1;
  }

  partial_update_recent_senders(&partials[i],peer_prefix);
  
  int piece_end=piece_offset+piece_bytes;

  // Note stream length if this is an end piece or journal bundle
  if (is_end_piece) {
    if (is_manifest_piece)
      partials[i].manifest_length=piece_end;
    else
      partials[i].body_length=piece_end;
  }
  if (version<0x100000000LL) {
    // Journal bundle, so body_length = version
    partials[i].body_length=version;
  }

  if ((bundle_number>-1)
      &&(!partials[i].body_segments)) {
    // This is a bundle that for which we already have a previous version, and
    // for which we as yet have no body segments.  So fetch from Rhizome the content
    // that we do have, and prepopulate the body segment.
    fprintf(stderr,"%s:%d:My SID as hex is %s\n",__FILE__,__LINE__,my_sid_hex);
    if (!prime_bundle_cache(bundle_number,my_sid_hex,servald_server,credential)) {
      struct segment_list *s=calloc(1,sizeof(struct segment_list));
      assert(s);
      s->data=malloc(cached_body_len);
      assert(s->data);
      bcopy(cached_body,s->data,cached_body_len);
      s->start_offset=0;
      s->length=cached_body_len;
      partials[i].body_segments=s;
      if (debug_pieces)
	printf("Preloaded %d bytes from old version of journal bundle.\n",
		cached_body_len);
    } else {
      if (debug_pieces)
	printf("Failed to preload bytes from old version of journal bundle. XFER will likely fail due to far end thinking it can skip the bytes we already have, so ignoring current piece.\n");
      return -1;
    }
  }

  // Now we have the right partial, we need to look for the right segment to add this
  // piece to, if any.
  struct segment_list **s;
  if (is_manifest_piece) s=&partials[i].manifest_segments;
  else s=&partials[i].body_segments;

  /*
    The segment lists are maintained in reverse order, since pieces will generally
    arrive in ascending address order.
  */
  int segment_start;
  int segment_end;
  while(1) {
    if (*s) {
      segment_start=(*s)->start_offset;
      segment_end=segment_start+(*s)->length;
    } else {
      segment_start=-1; segment_end=-1;
    }
    
    if ((!(*s))||(segment_end<piece_offset)) {
      // Create a new segment before the current one
      new_bytes_in_piece=piece_bytes;

      if (debug_pieces) printf("Inserting piece [%lld..%lld) before [%d..%d)\n",
		     piece_offset,piece_offset+piece_bytes,
		     segment_start,segment_end);

      struct segment_list *ns=calloc(1,sizeof(struct segment_list));
      assert(ns);

      // Link into the list
      ns->next=*s;
      if (*s) ns->prev=(*s)->prev; else ns->prev=NULL;
      if (*s) (*s)->prev=ns;
      *s=ns;

      // Set start and ends and allocate and copy in piece data
      ns->start_offset=piece_offset;
      ns->length=piece_bytes;
      ns->data=malloc(piece_bytes);
      bcopy(piece,ns->data,piece_bytes);

      // This is data that is new, and the next byte would also be new, so
      // no need to tell the peer to change where they are sending from in the bundle.
      next_byte_would_be_useful=1;
      
      break;
    } else if ((segment_start<=piece_offset)&&(segment_end>=piece_end)) {
      // Piece fits entirely within a current segment, i.e., is not new data
      new_bytes_in_piece=0;
      break;
    } else if (piece_end<segment_start) {
      // Piece ends before this segment starts, so proceed down the list further.
      if (debug_pieces)
	printf("Piece [%lld..%lld) comes before [%d..%d)\n",
		piece_offset,piece_offset+piece_bytes,
		segment_start,segment_end);
      
      s=&(*s)->next;
    } else {
      // Segment should abutt or overlap with new piece.
      // Pieces can be different sizes, so it is possible to extend both directions
      // at once.

      // New piece and existing segment should overlap or adjoin.  Otherwise abort.
      int piece_start=piece_offset;
      assert( ((segment_start>=piece_start)&&(segment_start<=piece_end))
	      ||((segment_end>=piece_start)&&(segment_end<=piece_end))
	      );      
      if (0)
      {
	message_buffer_length+=
	  snprintf(&message_buffer[message_buffer_length],
		   message_buffer_size-message_buffer_length,
		   "Received %s",bid_prefix);
	message_buffer_length+=
	  snprintf(&message_buffer[message_buffer_length],
		   message_buffer_size-message_buffer_length,
		   "* version %lld %s segment [%d,%d)\n",
		   version,
		   is_manifest_piece?"manifest":"payload",
		   piece_start,piece_start+piece_bytes);
      }
            
      if (piece_start<segment_start) {
	// Need to stick bytes on the start
	int extra_bytes=segment_start-piece_start;
	int new_length=(*s)->length+extra_bytes;
	unsigned char *d=malloc(new_length);
        assert(d);
	bcopy(piece,d,extra_bytes);
	bcopy((*s)->data,&d[extra_bytes],(*s)->length);
	(*s)->start_offset=piece_start;
	(*s)->length=new_length;
	free((*s)->data); (*s)->data=d;
	new_bytes_in_piece+=extra_bytes;
      }
      if (piece_end>segment_end) {
	// Need to sick bytes on the end
	int extra_bytes=piece_end-segment_end;
	int new_length=(*s)->length+extra_bytes;
	(*s)->data=realloc((*s)->data,new_length);
        assert((*s)->data);
	bcopy(&piece[piece_bytes-extra_bytes],&(*s)->data[(*s)->length],
	      extra_bytes);
	(*s)->length=new_length;
	new_bytes_in_piece+=extra_bytes;

	// We have extended beyond the end, so the next byte is most likely
	// useful, unless it happens to extend to the start of the next segment.
	// XXX - We are ignoring that case for now, as worst it will cause only
	// one wasted packet.  But it would be nice to detect this situation.
	next_byte_would_be_useful=1;
      }
      
      break;
    } 
  }

  merge_segments(&partials[i].manifest_segments);
  merge_segments(&partials[i].body_segments);
  partial_update_request_bitmap(&partials[i]);
  fprintf(stderr,"(Piece was [%lld,%lld)\n",piece_offset,piece_offset+piece_bytes);

  partials[i].recent_bytes += piece_bytes;
  
  // Check if we have the whole bundle now
  // XXX - this breaks when we have nothing about the bundle, because then we think the length is zero, so we think we have it all, when really we have none.
  if (partials[i].manifest_segments
      &&partials[i].body_segments
      &&(!partials[i].manifest_segments->next)
      &&(!partials[i].body_segments->next)
      &&(partials[i].manifest_segments->start_offset==0)
      &&(partials[i].body_segments->start_offset==0)
      &&(partials[i].manifest_segments->length
	 ==partials[i].manifest_length)
      &&(partials[i].body_segments->length
	 ==partials[i].body_length))
    {
      // We have a single segment for body and manifest that span the complete
      // size.
      printf(">>> %s We have the entire bundle %s*/%lld now.\n",
	     timestamp_str(),bid_prefix,version);

      // First, reconstitute the manifest from the binary encoded format
      unsigned char manifest[1024];
      int manifest_len;

      int insert_result=-999;
      
      if (!manifest_binary_to_text
	  (partials[i].manifest_segments->data,
	   partials[i].manifest_length,
	   manifest,&manifest_len)) {

	// Display decompressed manifest
	dump_bytes(stdout,"Decompressed Manifest",manifest,manifest_len);
	
	insert_result=
	  rhizome_update_bundle(manifest,manifest_len,
				partials[i].body_segments->data,
				partials[i].body_length,
				servald_server,credential);

	if (debug_bundlelog) {
	  // Write details of bundle to a log file for monitoring
	  // This is used for rhizome velocity experiments.  For that purpose,
	  // we like to know the name of the bundle we are looking for, so we include
	  // it in the message.
	  fprintf(stderr,"Logging new bundle...\n");
	  FILE *bundlelogfile=fopen(bundlelog_filename,"a");
	  if (bundlelogfile) {
	    char bid[1024];
	    char version[1024];
	    char filename[1024];
	    char message[1024];
	    char filesize[1024];
	    char service[1024];
	    char sender[1024];
	    char recipient[1024];
	    time_t now=time(0);
	    manifest_get_field(manifest,manifest_len,"name",filename);
	    manifest_get_field(manifest,manifest_len,"id",bid);
	    manifest_get_field(manifest,manifest_len,"version",version);
	    manifest_get_field(manifest,manifest_len,"filesize",filesize);
	    manifest_get_field(manifest,manifest_len,"service",service);	    
	    manifest_get_field(manifest,manifest_len,"sender",sender);
	    manifest_get_field(manifest,manifest_len,"recipient",recipient);
	    snprintf(message,1024,"%lld:T+%lldms:BUNDLERX:%s:%s/%s:%s:%s:%s:%s:%s:%s",
		     (long long)now,(long long)(gettime_ms()-start_time),
		     my_sid_hex,bid,version,filename,filesize,service,sender,recipient,ctime(&now));
	    fprintf(bundlelogfile,"%s",message);
	    fclose(bundlelogfile);
	  }
	  fprintf(stderr,"Done logging new bundle.\n");
	}

	// Take note of the bundle, so that we can tell any peer who is trying to
	// send it to us, that we have recently received it.  This is irrespective
	// of whether it inserted correctly. The reasoning behind this, is that we
	// don't want a peer to get stuck sending the same bundle over and over
	// again.  It is better to send something else, and work through all the
	// bundles that need sending first. Then after that, if we restart our sync
	// process periodically, we will catch any straglers. It still isn't perfect,
	// but it's a start.
	sync_remember_recently_received_bundle
	  (partials[i].bid_prefix,
	   partials[i].bundle_version);
      } else {
	printf(">>> %s Could not decompress binary manifest.  Not inserting\n",
	       timestamp_str());
	// This will cause us to try to receive the entire bundle again, not just the
	// manifest.
	// XXX - Decompress manifest as soon as we have it to catch this problem
	// earlier. 
      }
      if (insert_result) {
	// Failed to insert, so mark this bundle for deprioritisation, so that we
	// don't just keep asking for it.
	fprintf(stderr,"Failed to insert bundle %s*/%lld (result=%d)\n",
		partials[i].bid_prefix,
		partials[i].bundle_version,insert_result);
	dump_bytes(stdout,"manifest",manifest,manifest_len);
	dump_bytes(stdout,"payload",
		   partials[i].body_segments->data,
		   partials[i].body_length);

	char bid[32*2+1];
	if (!manifest_extract_bid(partials[i].manifest_segments->data,
				  bid)) {
#ifdef SYNC_BY_BAR
	  int bundle=bid_to_peer_bundle_index(peer,bid);
	  if (peer_records[peer]->insert_failures[bundle]<255)
	    peer_records[peer]->insert_failures[bundle]++;
#endif
	}
      } else {
	// Insert succeeded, so clear any failure deprioritisation (although it
	// shouldn't matter).
	char bid[32*2+1];
	if (!manifest_extract_bid(partials[i].manifest_segments->data,
				  bid)) {
#ifdef SYNC_BY_BAR
	  int bundle=bid_to_peer_bundle_index(peer,bid);
	  peer_records[peer]->insert_failures[bundle]=0;
#endif
	}
	progress_log_bundle_receipt(partials[i].bid_prefix,
				    partials[i].bundle_version);
      }

      // Tell peer we have the whole thing now.
      // (next_byte_would_be_useful is asserted so that we don't send two
      // reports).
      next_byte_would_be_useful=1;
      sync_tell_peer_we_have_the_bundle_of_this_partial(peer,i);
      
      // Now release this partial.
      clear_partial(&partials[i]);
    }
  else {
    // To deal with multiple senders that are providing us with pieces in lock-step,
    // we want to be able to redirect them to send from different positions in the bundle.
    // XXX - Does this mean that we will never have to deal with next_byte_would_be_useful==0 ?
    if (option_flags&FLAG_NO_BITMAP_PROGRESS) {
      if (!new_bytes_in_piece)
	sync_schedule_progress_report(peer,i,1 /* random jump */);
      else if (!next_byte_would_be_useful)
	sync_schedule_progress_report(peer,i,0 /* send from first required byte */);
    } else {
      fprintf(stderr,"Sending BITMAP\n");
      sync_schedule_progress_report_bitmap(peer,i);
    }
  }
  
  return 0;
}


#define message_parser_51 message_parser_50
#define message_parser_70 message_parser_50
#define message_parser_71 message_parser_50

int message_parser_50(struct peer_state *sender,char *sender_prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  int offset=0;

  char bid_prefix[8*2+1];
  long long version;
  unsigned int offset_compound;
  long long piece_offset;
  int piece_bytes;
  int piece_is_manifest;
  int above_1mb=0;
  int is_end_piece=0;
  int for_me=0;

  // Skip header character
  if (!(msg[offset]&0x20)) above_1mb=1;
  if (!(msg[offset]&0x01)) is_end_piece=1;
  offset++;
  
  // Work out from target SID, if this is intended for us
  if ((my_sid[0]!=msg[offset])||(my_sid[1]!=msg[offset+1])) for_me=0;
  else for_me=1;
  offset+=2;
  
  if (length-offset<(1+8+8+4+1)) return -3;
  unsigned char *bid_prefix_bin=&msg[offset];
  snprintf(bid_prefix,8*2+1,"%02x%02x%02x%02x%02x%02x%02x%02x",
	   msg[offset+0],msg[offset+1],msg[offset+2],msg[offset+3],
	   msg[offset+4],msg[offset+5],msg[offset+6],msg[offset+7]);
  offset+=8;
  version=0;
  for(int i=0;i<8;i++) version|=((long long)msg[offset+i])<<(i*8LL);
  offset+=8;
  offset_compound=0;
  for(int i=0;i<6;i++) offset_compound|=((long long)msg[offset+i])<<(i*8LL);
  offset+=4;
  if (above_1mb) offset+=2; else offset_compound&=0xffffffff;
  piece_offset=(offset_compound&0xfffff)|((offset_compound>>12LL)&0xfff00000LL);
  piece_bytes=(offset_compound>>20)&0x7ff;
  piece_is_manifest=offset_compound&0x80000000;
  
  if (monitor_mode)
    {
      char sender_prefix[128];
      char monitor_log_buf[1024];
      sprintf(sender_prefix,"%s*",sender->sid_prefix);
      snprintf(monitor_log_buf,sizeof(monitor_log_buf),
	       "Piece of bundle: BID=%s*, [%lld--%lld) of %s.%s",
	       bid_prefix,
	       piece_offset,piece_offset+piece_bytes-1,
	       piece_is_manifest?"manifest":"payload",
	       is_end_piece?" This is the last piece of that.":""
	       );
      
      monitor_log(sender_prefix,NULL,monitor_log_buf);
    }
  
  saw_piece(sender_prefix,for_me,
	    bid_prefix,bid_prefix_bin,
	    version,piece_offset,piece_bytes,is_end_piece,
	    piece_is_manifest,&msg[offset],
	    prefix, servald_server,credential);
  
  if (piece_bytes>0) offset+=piece_bytes;
    
  return offset;
}

