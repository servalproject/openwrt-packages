/*
  Serval Low-bandwidth asychronous Rhizome Demonstrator.
  Copyright (C) 2016 Serval Project Inc.

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

extern char *my_sid_hex;

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <assert.h>

#include "sync.h"
#include "lbard.h"
#include "sha1.h"
#include "util.h"

int report_queue_length=0;
uint8_t report_queue[REPORT_QUEUE_LEN][MAX_REPORT_LEN];
uint8_t report_lengths[REPORT_QUEUE_LEN];
struct peer_state *report_queue_peers[REPORT_QUEUE_LEN];
int report_queue_partials[REPORT_QUEUE_LEN];
char *report_queue_message[REPORT_QUEUE_LEN];

int bundle_calculate_tree_key(sync_key_t *bundle_tree_key,
			      uint8_t sync_tree_salt[SYNC_SALT_LEN],
			      char *bid,
			      long long version,
			      long long length,
			      char *filehash)
{
  /*
    Calculate a sync key for this bundle.
    Sync keys are relatively short, only 64 bits, as this is still sufficient to
    maintain a very low probability of colissions, provided that each peer has less
    than 2^32 bundles.

    Ideally we would hash the entire manifest of a bundle, but that would require us
    to retrieve each manifest, and we would rather not require that.  So instead we
    will use the BID, length, version and filehash as inputs.  This combination means
    that in the unlikely event of a colission, updating the bundle is almost certain
    to resolve the colission.  Thus the natural human response of sending another
    message if the first doesn't get through is likely to resolve the problem.

    The relatively small key space does however leave us potentially vulnerable to a
    determined adversary to find coliding hashes to disrupt communications. We will
    need to do something about this in due course. This could probably be protected
    by using a random salt between pairs of peers when they do the sync process, so
    that colissions cannot be arranged ahead of time.

    Using a salt, and changing it periodically, would also provide implicit protection
    against colissions of any source, so it is probably a really good idea to
    implement.  The main disadvantage is that we need to calculate all the hashes for
    all the bundles we hold whenever we talk to a new peer.  We could, however,
    use a salt for all peers which we update periodically, to offer a good compromise
    between computational cost and protection against accidental and intentional
    colissions.
    
    Under this strategy, we need to periodically recalculate the sync key, i.e, hash,
    for each bundle we hold, and invalidate the sync tree for each peer when we do so.

    ... HOWEVER, we need both sides of a conversation to have the same salt, which
    wouldn't work under that scheme.

    So for now we will employ a salt, but it will probably be fixed until we decide on
    a good solution to this set of problems.
  */

  char lengthstring[80];
  snprintf(lengthstring,80,"%llx:%llx",length,version);
  
  struct sha1nfo sha1;  
  sha1_init(&sha1);
  sha1_write(&sha1,(const char *)sync_tree_salt,SYNC_SALT_LEN);
  sha1_write(&sha1,bid,strlen(bid));
  sha1_write(&sha1,filehash,strlen(filehash));
  sha1_write(&sha1,lengthstring,strlen(lengthstring));
  unsigned char *res=sha1_result(&sha1);
  bcopy(res,bundle_tree_key->key,KEY_LEN);
  return 0;  
}

int sync_tree_receive_message(struct peer_state *p,unsigned char *msg)
{
  int len=msg[1];

  if (debug_sync)
    printf("Receiving sync tree message of %d bytes\n",len);
      
  // Pull out the sync tree message for processing.
  int sync_bytes=len-SYNC_MSG_HEADER_LEN;

  if (debug_sync_keys) {
    char filename[1024];
    snprintf(filename,1024,"lbardkeys.%s.received_sync_message",my_sid_hex);
    FILE *f=fopen(filename,"a");
    fprintf(f,"%d:",len-2);
    for(int i=0;i<(len-2);i++) fprintf(f,"%02X ",msg[i+2]);
    fprintf(f,"\n");
    fclose(f);
  }

  if (debug_sync) {
    printf(">>> %s Calling sync_recv_message(len=%d)\n",
	   timestamp_str(),sync_bytes);
    dump_bytes(stdout,"Sync message",&msg[SYNC_MSG_HEADER_LEN], sync_bytes);
  }
  
  sync_recv_message(sync_state,(void *)p,&msg[SYNC_MSG_HEADER_LEN], sync_bytes);
  if (debug_sync) 
    printf(">>> %s sync_recv_message() returned.\n",timestamp_str());
  
  return 0;
}

int sync_announce_bundle_piece(int peer,int *offset,int mtu,
			       unsigned char *msg,
			       char *sid_prefix_hex,
			       char *servald_server, char *credential)
{
  int bundle_number=peer_records[peer]->tx_bundle;
  if (debug_ack)
    fprintf(stderr,"HARDLOWER: Announcing a piece of bundle #%d\n",bundle_number);
  if (bundle_number<0) return -1;
  
  if (prime_bundle_cache(bundle_number,
			 sid_prefix_hex,servald_server,credential)) {
    peer_records[peer]->tx_cache_errors++;
    if (peer_records[peer]->tx_cache_errors>MAX_CACHE_ERRORS)
      {
	sync_dequeue_bundle(peer_records[peer],peer_records[peer]->tx_bundle);
      }
    if (debug_ack)
      fprintf(stderr,"HARDLOWER: Couldn't prime bundle cache.\n");
    return -1;
  }
  else
    peer_records[peer]->tx_cache_errors=0;

  // Update send point based on the bundle progress bitmap for this peer, if we
  // have one.
  if (!(option_flags&FLAG_NO_BITMAP_PROGRESS)) peer_update_send_point(peer);
  
  // Mark manifest all sent once we get to the end
  if (peer_records[peer]->tx_bundle_manifest_offset>=cached_manifest_encoded_len)
    peer_records[peer]->tx_bundle_manifest_offset=1024;

  // Send piece of manifest, if required
  // (but never from an offset before the hard lower bound communicated in an ACK('A') message
  if (!(option_flags&FLAG_NO_HARD_LOWER))
    if (peer_records[peer]->tx_bundle_manifest_offset
	<peer_records[peer]->tx_bundle_manifest_offset_hard_lower_bound) {
      fprintf(stderr,"HARD_LOWER: Advancing manifest tx offset from %d to %d\n",
	      peer_records[peer]->tx_bundle_manifest_offset,
	      peer_records[peer]->tx_bundle_manifest_offset_hard_lower_bound);
      peer_records[peer]->tx_bundle_manifest_offset
	=peer_records[peer]->tx_bundle_manifest_offset_hard_lower_bound;
    }
  
  if (peer_records[peer]->tx_bundle_manifest_offset_hard_lower_bound<cached_manifest_encoded_len) {
    if (peer_records[peer]->tx_bundle_manifest_offset<cached_manifest_encoded_len) {
      fprintf(stderr,"  manifest_offset=%d, manifest_len=%d\n",
	      peer_records[peer]->tx_bundle_manifest_offset,
	      cached_manifest_encoded_len);
      int start_offset=peer_records[peer]->tx_bundle_manifest_offset;
      int bytes =
	sync_append_some_bundle_bytes(bundle_number,start_offset,
				      cached_manifest_encoded_len,
				      &cached_manifest_encoded[start_offset],1,
				      offset,mtu,msg,peer);
      if (bytes>0)
	peer_records[peer]->tx_bundle_manifest_offset+=bytes;
    }
  }

  // Announce the length of the body if we have finished sending the manifest,
  // but not yet started on the body.  This is really just to help monitoring
  // the progress of transfers for debugging.  The transfer process will automatically
  // detect the end of the bundle when the last piece is received.
  if (peer_records[peer]->tx_bundle_manifest_offset>=cached_manifest_encoded_len) {
    // Send length of body?
    if (((!peer_records[peer]->tx_bundle_body_offset)
	 ||(peer_records[peer]->tx_bundle_body_offset
	    ==peer_records[peer]->tx_bundle_body_offset_hard_lower_bound)
	 )
	||(peer_records[peer]->tx_bundle_body_offset>=cached_body_len))
      {
	fprintf(stderr,"T+%lldms : Sending length of bundle %s (bundle #%d, version %lld, cached_version %lld)\n",
		gettime_ms()-start_time,
		bundles[bundle_number].bid_hex,
		bundle_number,bundles[bundle_number].version,
		cached_version);
	announce_bundle_length(mtu,msg,offset,bundles[bundle_number].bid_bin,cached_version,bundles[bundle_number].length);
      }
  }
  {
    // Send some of the body
    // (but never from an offset before the hard lower bound communicated in an ACK('A') message
    if (!(option_flags&FLAG_NO_HARD_LOWER)) {
      if (peer_records[peer]->tx_bundle_body_offset
	  <peer_records[peer]->tx_bundle_body_offset_hard_lower_bound)
	{
	  if (debug_ack)
	    fprintf(stderr,"HARDLOWER: Advancing tx_bundle_body_offset from %d to %d\n",
		    peer_records[peer]->tx_bundle_body_offset,
		    peer_records[peer]->tx_bundle_body_offset_hard_lower_bound);
	  peer_records[peer]->tx_bundle_body_offset
	    =peer_records[peer]->tx_bundle_body_offset_hard_lower_bound;
	}
    }
    
    if (debug_ack)
      fprintf(stderr,"HARDLOWER: Sending body piece with body_offset=%d, body_len=%d (hard lower limit = %d/%d\n",
	      peer_records[peer]->tx_bundle_body_offset,
	      cached_body_len,
	      peer_records[peer]->tx_bundle_manifest_offset_hard_lower_bound,
	      peer_records[peer]->tx_bundle_body_offset_hard_lower_bound
	      );
    int start_offset=peer_records[peer]->tx_bundle_body_offset;
    
    int bytes =
      sync_append_some_bundle_bytes(bundle_number,start_offset,cached_body_len,
				    &cached_body[start_offset],0,
				    offset,mtu,msg,peer);
    
    if (bytes>0)
      peer_records[peer]->tx_bundle_body_offset+=bytes;      
  }
  
  // If we have sent to the end of the bundle, then start again from the beginning,
  // until the peer acknowledges that they have received it all (or tells us to
  // start sending again from a different part of the bundle).
  // (the _hard_lower_bound values are used to advance the loop-back point from the
  // beginning of the bundle to the appropriate place, if partial reception has been
  // acknowledged.
  if ((peer_records[peer]->tx_bundle_body_offset>=bundles[bundle_number].length)
      &&(peer_records[peer]->tx_bundle_manifest_offset>=cached_manifest_encoded_len))
    {
      peer_records[peer]->tx_bundle_body_offset=0;
      peer_records[peer]->tx_bundle_manifest_offset=0;
      fprintf(stderr,"T+%lldms : Resending bundle %s from the start.\n",
	      gettime_ms()-start_time,
	      bundles[bundle_number].bid_hex);

    }
  
  return 0;
}

int sync_tree_send_data(int *offset,int mtu, unsigned char *msg_out,int peer,
			char *sid_prefix_hex,char *servald_server,char *credential)
{
  /*
    Send a piece of the bundle (manifest or body) to this peer, for the highest
    priority bundle that we have that we believe that they don't have.
    (If they have it, then they will acknowledge the entirety of it, allowing us
    to advance to the next bundle.)

  */
  if (peer_records[peer]->tx_bundle>-1)
    {
      // Try to also send a piece of body, even if we have already stuffed some
      // manifest in, because we might still have space.      
      sync_announce_bundle_piece(peer,offset,mtu,msg_out,
				 sid_prefix_hex,servald_server,credential);
    }
  return 0;
}

int sync_by_tree_stuff_packet(int *offset,int mtu, unsigned char *msg_out,
			      char *sid_prefix_hex,
			      char *servald_server,char *credential)
{
  // Stuff packet as full as we can with data for as many peers as we can.
  // In practice, we will likely fill it on the first peer, but let's not
  // waste a packet if we have something we can stuff in.

  // First of all, tell any peers any acknowledgement messages that are required.
  while (report_queue_length&&((*offset)<(mtu-MAX_REPORT_LEN))) {
    report_queue_length--;
    if (append_bytes(offset,mtu,msg_out,report_queue[report_queue_length],
		     report_lengths[report_queue_length])) {
      fprintf(stderr,"Tried to send report_queue message '%s' to %s*, but append_bytes reported no more space.\n",
	      report_queue_message[report_queue_length],
	      report_queue_peers[report_queue_length]->sid_prefix);
      report_queue_length++;
    } else {
      fprintf(stderr,">>> %s Flushing %d byte report from queue, %d remaining.\n",
	      timestamp_str(),	      
	      report_lengths[report_queue_length],
	      report_queue_length);
      fprintf(stderr,"T+%lldms : Flushing %d byte report from queue, %d remaining.\n",
	      gettime_ms()-start_time,
	      report_lengths[report_queue_length],
	      report_queue_length);
      fprintf(stderr,"Sent report_queue message '%s' to %s*\n",
	      report_queue_message[report_queue_length],
	      report_queue_peers[report_queue_length]->sid_prefix);
      dump_bytes(stderr,"report_queue message",
		 (unsigned char *)report_queue_message[report_queue_length],
		 report_lengths[report_queue_length]);
		 
      free(report_queue_message[report_queue_length]);
      report_queue_message[report_queue_length]=NULL;
    }
  }
  
  int count=10; if (count>peer_count) count=peer_count;

  /* Try sending something new.
     Sync trees, and if space remains (because we have synchronised trees),
     then try sending a piece of a bundle, if any */
  int sync_not_sent=1;
  if (random()&1) {
    sync_not_sent=0;
    sync_tree_send_message(offset,mtu,msg_out);
  }
  
  while((*offset)<(mtu-16)) {
    if ((count--)<0) break;
    int peer=random_active_peer();
    if (peer<0) break;
    int space=mtu-(*offset);
    if (space>10) {
      sync_tree_send_data(offset,mtu,msg_out,peer,
			  sid_prefix_hex,servald_server,credential);
    } else {
      // No space -- can't do anything
    }
  }  

  if (sync_not_sent)
    // Don't waste any space: sync what we can
    sync_tree_send_message(offset,mtu,msg_out);
  
  return 0;
}

int sync_tree_populate_with_our_bundles()
{
  for(int i=0;i<bundle_count;i++)
    sync_add_key(sync_state,&bundles[i].sync_key,&bundles[i]);
  return 0;
}

int sync_tell_peer_we_have_this_bundle(int peer, int bundle)
{
  return sync_tell_peer_we_have_bundle_by_id(peer,bundles[bundle].bid_bin,
					     bundles[bundle].version);
}
  
int sync_tell_peer_we_have_bundle_by_id(int peer,unsigned char *bid,long long version)
{
  int slot=report_queue_length;

  for(int i=0;i<report_queue_length;i++) {
    if (report_queue_peers[i]==peer_records[peer]) {
      // We already want to tell this peer something.
      // We should only need to tell a peer one thing at a time.
      slot=i; break;
    }
  }
  
  if (slot>=REPORT_QUEUE_LEN) slot=random()%REPORT_QUEUE_LEN;

  // Mark utilisation of slot, so that we can flush out stale messages
  report_queue_partials[slot]=-1;
  report_queue_peers[slot]=peer_records[peer];

  sync_build_bar_in_slot(slot,bid,version);

  if (!monitor_mode) {
    if (report_queue_message[slot]) {
      fprintf(stderr,"Replacing report_queue message '%s' with 'BAR'\n",
	      slot>=0?(report_queue_message[slot]?report_queue_message[slot]:"< report_queue_message=slot >"):"< slot<0 >");
      free(report_queue_message[slot]);
      report_queue_message[slot]=NULL;
    } else
      fprintf(stderr,"Setting report_queue message to 'BAR'\n");
    report_queue_message[slot]=strdup("BAR");
  }
  
  if (slot>=report_queue_length) report_queue_length=slot+1;

  return 0;
}

unsigned char bin_prefix[8];
unsigned char *bid_prefix_hex_to_bin(char *hex)
{
  for(int i=0;i<8;i++) {
    char h[3]={hex[i*2+0],hex[i*2+1],0};
    bin_prefix[i]=strtoll(h,NULL,16);
  }
  return bin_prefix;
}

int sync_tell_peer_we_have_the_bundle_of_this_partial(int peer, int partial)
{

  return sync_tell_peer_we_have_bundle_by_id
    (peer,bid_prefix_hex_to_bin(partials[partial].bid_prefix),
     partials[partial].bundle_version);  
}


int lookup_bundle_by_prefix(const unsigned char *prefix,int len)
{
  if (len>8) len=8;
  
  int best_bundle=-1;
  int bundle;
  int i;
  for(bundle=0;bundle<bundle_count;bundle++) {
    for(i=0;i<len;i++) {
      if (prefix[i]!=bundles[bundle].bid_bin[i]) break;
    }
    if (i==len) {
      if ((best_bundle==-1)||(bundles[bundle].version>bundles[best_bundle].version))
	best_bundle=bundle;      
    }
  }
  if (0)
    printf("  %02X%02X%02X%02x* is bundle #%d of %d\n",
	   prefix[0],prefix[1],prefix[2],prefix[3],
	   best_bundle,bundle_count);
  return best_bundle;
}

int lookup_bundle_by_prefix_bin_and_version_exact(unsigned char *prefix, long long version)
{
  int bundle;
  int i;
  for(bundle=0;bundle<bundle_count;bundle++) {
    for(i=0;i<8;i++) {
      if (prefix[i]!=bundles[bundle].bid_bin[i]) break;
    }
    if (i==8) {
      if (bundles[bundle].version==version)
	return bundle;
    }
  }
  return -1;
}

// Returns newest bundle version of relevance
int lookup_bundle_by_prefix_bin_and_version_or_newer(unsigned char *prefix, long long version)
{
  int best_bundle=-1;
  int bundle;
  int i;
  for(bundle=0;bundle<bundle_count;bundle++) {
    for(i=0;i<8;i++) {
      if (prefix[i]!=bundles[bundle].bid_bin[i]) break;
    }
    if (i==8) {
      if (bundles[bundle].version>=version) {
	if ((best_bundle==-1)||(bundles[bundle].version>bundles[best_bundle].version))
	  best_bundle=bundle;
      }
      
    }
  }
  return best_bundle;
}

int lookup_bundle_by_prefix_bin_and_version_or_older(unsigned char *prefix, long long version)
{
  int bundle;
  int i;
  for(bundle=0;bundle<bundle_count;bundle++) {
    for(i=0;i<8;i++) {
      if (prefix[i]!=bundles[bundle].bid_bin[i]) break;
    }
    if (i==8) {
      if (bundles[bundle].version<=version)
	return bundle;
    }
  }
  return -1;
}


int sync_queue_bundle(struct peer_state *p,int bundle)
{
  struct bundle_record *b=&bundles[bundle];

  int priority=calculate_bundle_intrinsic_priority(b->bid_hex,
						   b->length,
						   b->version,
						   b->service,
						   b->recipient,
						   0);

  // TX queue has something in it.
  if (p->tx_bundle>=0) {
    if (priority>p->tx_bundle_priority) {
      // Bump current tx_bundle to TX queue, and substitute with this one.
      // (substitution happens below)
      if (p->tx_bundle!=bundles[p->tx_bundle].index) {
	fprintf(stderr,"WARNING: Bundle #%d has index set to %d\n",
		p->tx_bundle,bundles[p->tx_bundle].index);
	bundles[p->tx_bundle].index=p->tx_bundle;
      }
      peer_queue_bundle_tx(p,&bundles[p->tx_bundle],
			   p->tx_bundle_priority);
      p->tx_bundle=-1;
    } else {
      // Bump new bundle to TX queue
      peer_queue_bundle_tx(p,b,priority);
    }
  }

  // If nothing in the TX queue, just add it.
  // (also used to putting new bundle in the current TX slot if there was something
  // lower priority in there previously.)
  if (p->tx_bundle==-1) {
    // Start body transmission at a random point, so that if we are sending the
    // bundle to multiple peers, we at least have a chance of not sending the same
    // piece to each in a redundant manner. It would be even better to have some
    // awareness of when we are doing this, so that we can optimise it. XXX
    // XXX - The benefit of this disappears as soon as the peer requests a
    // re-transmission, since it will start requesting from the earliest byte that it
    // lacks.

    for(int i=0;i<peer_count;i++)
      if ((p!=peer_records[i])&&(peer_records[i]->tx_bundle==bundle)) {
	// We are already sending this bundle to someone else -- try to keep
	// it in sync
	p->tx_bundle=bundle;
	p->tx_bundle_body_offset=peer_records[i]->tx_bundle_body_offset;
	p->tx_bundle_manifest_offset=peer_records[i]->tx_bundle_manifest_offset;
	p->tx_bundle_priority=priority;
	fprintf(stderr,"Beginning transmission from same offset as for another peer (m=%d, b= %d)\n",
		p->tx_bundle_manifest_offset,p->tx_bundle_body_offset);
	return 0;
      }

    // Not already sending to another peer, so just pick a random point and start
    p->tx_bundle=bundle;
    if (!(option_flags&FLAG_NO_HARD_LOWER)) {
      if (debug_ack)
	fprintf(stderr,"HARDLOWER: Resetting hard lower start point to 0,0\n");
    }
    p->tx_bundle_manifest_offset_hard_lower_bound=0;
    p->tx_bundle_body_offset_hard_lower_bound=0;
    if (bundles[bundle].length)
      p->tx_bundle_body_offset=(random()%bundles[bundle].length)&0xffffff00;
    else
      p->tx_bundle_body_offset=0;
    if (option_flags&FLAG_NO_RANDOMIZE_START_OFFSET)
      p->tx_bundle_body_offset=0;
    // ... but start from the beginning if it will take only one packet
    if (bundles[bundle].length<150) p->tx_bundle_body_offset=0;
    prime_bundle_cache(bundle,p->sid_prefix,servald_server,credential);
    if (cached_manifest_encoded_len)
      p->tx_bundle_manifest_offset=(random()%cached_manifest_encoded_len)&0xffffff80;
    if (option_flags&FLAG_NO_RANDOMIZE_START_OFFSET)
      p->tx_bundle_manifest_offset=0;
    p->tx_bundle_priority=priority;
    fprintf(stderr,"Beginning transmission from random offset (m=%d, p=%d), flags=%d\n",
	    p->tx_bundle_manifest_offset,p->tx_bundle_body_offset,
	    option_flags);
  }

  // peer_queue_list_dump(p);
  return 0;
}


int sync_dequeue_bundle(struct peer_state *p,int bundle)
{
  if (!p) return -1;
  
  int peer=0;
  for(peer=0;peer<peer_count;peer++)
    if (p==peer_records[peer]) break;
  if (peer>=peer_count) return -1;
  
  printf("Dequeuing TX of bundle #%d (",bundle);
  describe_bundle(RESOLVE_SIDS,stdout,NULL,bundle,
		  peer,-1,-1);
  printf(") to %s*\n",p->sid_prefix);
  
  
  if (bundle==p->tx_bundle) {
    // Delete this entry in queue
    p->tx_bundle=-1;
    // Advance next in queue, if there is anything
    if (p->tx_queue_len) {
      if (debug_ack)
	fprintf(stderr,"HARDLOWER: DEQUEUING:\n     %d more bundles in the queue. Next is bundle #%d\n",
		p->tx_queue_len,p->tx_queue_bundles[0]);
      p->tx_bundle=p->tx_queue_bundles[0];
      p->tx_bundle_priority=p->tx_queue_priorities[0];
      p->tx_bundle_manifest_offset=0;
      p->tx_bundle_body_offset=0;      
      p->tx_bundle_manifest_offset_hard_lower_bound=0;
      p->tx_bundle_body_offset_hard_lower_bound=0;
      if (!(option_flags&FLAG_NO_HARD_LOWER)) {
	if (debug_ack)
	  fprintf(stderr,"HARDLOWER: Resetting hard lower start point to 0,0\n");
      }
      bcopy(&p->tx_queue_bundles[1],
	    &p->tx_queue_bundles[0],
	    sizeof(int)*p->tx_queue_len-1);
      bcopy(&p->tx_queue_priorities[1],
	    &p->tx_queue_priorities[0],
	    sizeof(int)*p->tx_queue_len-1);
      p->tx_queue_len--;
    } else {
      if (p->tx_queue_overflow) {
	/* TX queue overflowed at some point, and now we have
	   emptied the queue. This means that we need to restart
	   the tree synchronisation with this peer.  
	   XXX - In time, Jeremy will implement sync tree enumeration,
	   which will be a more efficient solution to this problem.
	   In the meantime, we will just change our instance ID, and also
	   the instance ID we have recorded for this peer, so that we
	   force a re-sync.
	*/
	p->instance_id=0xffffffff;
	my_instance_id=0;
	while(my_instance_id==0)
	  urandombytes((unsigned char *)&my_instance_id,sizeof(unsigned int));
      }
    }
  } else {
    // Wasn't the bundle on the list right now, so delete from in list.
    for(int i=0;i<p->tx_queue_len;i++) {
      if (bundle==p->tx_queue_bundles[i]) {
	// printf("Before deletion from in queue:\n");
	// peer_queue_list_dump(p);
	// Delete this entry in queue
	bcopy(&p->tx_queue_bundles[i+1],
	      &p->tx_queue_bundles[i],
	      sizeof(int)*p->tx_queue_len-i-1);
	bcopy(&p->tx_queue_priorities[i+1],
	      &p->tx_queue_priorities[i],
	      sizeof(int)*p->tx_queue_len-i-1);
	p->tx_queue_len--;
	// printf("After deletion from in queue:\n");
	// peer_queue_list_dump(p);
	return 0;
      }
    }
    
  }

  return 0;
}


void peer_has_this_key(void *context, void *peer_context, const sync_key_t *key)
{
  struct peer_state *p=(struct peer_state *)peer_context;

  // Peer has something that we want.
  if (1) printf(">>> %s Peer %s* HAS some bundle that we don't have (key prefix=%02X%02X*).\n",
		timestamp_str(),p->sid_prefix,
		((unsigned char *)key)[0],((unsigned char *)key)[1]);

}

void peer_now_has_this_key(void *context, void *peer_context,void *key_context,
			   const sync_key_t *key)
{
  // Peer has something, that we also have. 
  // We should stop sending it to them, if we were trying.

  struct peer_state *p=(struct peer_state *)peer_context;
  struct bundle_record *b=(struct bundle_record*)key_context;

  // Verify that the bundle we are pointing to is still the correct bundle, and
  // that it's version hasn't changed.
  if (memcmp(&b->sync_key,key,sizeof(sync_key_t))) {
    printf(">>> %s Peer %s* now has older version of bundle %s* (key prefix=%02X%02x*)\n",
	   timestamp_str(),
	   p->sid_prefix,
	   b->bid_hex,
	   ((unsigned char *)key)[0],((unsigned char *)key)[1]);
    return;
  }
  
  if (1)
    printf(">>> %s Peer %s* now has bundle %s* (key prefix=%02X%02x*),"
	   " service=%s, version=%lld\n"
	   "    sender=%s,\n"
	   "    recipient=%s\n",
	   timestamp_str(),
	   p->sid_prefix,
	   b->bid_hex,
	   ((unsigned char *)key)[0],((unsigned char *)key)[1],
	   b->service,b->version,b->sender,b->recipient);
  
  sync_dequeue_bundle(p,b->index);

}


void peer_does_not_have_this_key(void *context, void *peer_context,void *key_context,
				 const sync_key_t *key)
{
  // We need to send something to a peer
  
  struct peer_state *p=(struct peer_state *)peer_context;
  struct bundle_record *b=(struct bundle_record*)key_context;

  if (debug_bundles)
    printf(">>> %s Peer %s* is missing bundle %s* (key prefix=%02X%02X*), "
	   "service=%s, version=%lld,"
	   " sender=%s,"
	   " recipient=%s\n",
	   timestamp_str(),
	   p->sid_prefix,
	   b->bid_hex,
	   ((unsigned char *)key)[0],((unsigned char *)key)[1],
	   b->service,b->version,b->sender,b->recipient);

  if (debug_sync_keys) {
    char filename[1024];
    snprintf(filename,1024,"lbardkeys.%s.needs.to.send.to.%s",my_sid_hex,p->sid_prefix);
    FILE *f=fopen(filename,"a");
    fprintf(f,"%02X%02X%02X%02X%02X%02X%02X%02X:%s:%016llX\n",
	    key->key[0],key->key[1],key->key[2],key->key[3],
	    key->key[4],key->key[5],key->key[6],key->key[7],
	    b->bid_hex,b->version);
    fclose(f);
  }
    
  sync_queue_bundle(p,b->index);
  
  return;  
}


int sync_setup()
{
  sync_state = sync_alloc_state(NULL,
				peer_has_this_key,
				peer_does_not_have_this_key,
				peer_now_has_this_key);
  return 0;
}

#define MAX_RECENT_BUNDLES 128
#define RECENT_BUNDLE_TIMEOUT (4*60)
struct recent_bundle recent_bundles[MAX_RECENT_BUNDLES];
int recent_bundle_count=0;

int sync_remember_recently_received_bundle(char *bid_prefix, long long version)
{
  int first_timed_out=-1;
  int i;
  for(i=0;i<recent_bundle_count;i++)
    if (!strcasecmp(bid_prefix,recent_bundles[i].bid_prefix)) {
      if (version>=recent_bundles[i].bundle_version)
	recent_bundles[i].bundle_version=version;
      recent_bundles[i].timeout=time(0)+RECENT_BUNDLE_TIMEOUT;
      return 0;
    } else {
      if (recent_bundles[i].timeout<time(0)) first_timed_out=i;
    }
  if (recent_bundle_count>=MAX_RECENT_BUNDLES) {
    if (first_timed_out==-1) i=random()%MAX_RECENT_BUNDLES;
    else i=first_timed_out;
    free(recent_bundles[i].bid_prefix); recent_bundles[i].bid_prefix=NULL;
  } else {
    i=recent_bundle_count;
    recent_bundle_count++;
  }

  recent_bundles[i].bid_prefix=strdup(bid_prefix);
  recent_bundles[i].bundle_version=version;
  recent_bundles[i].timeout=time(0)+RECENT_BUNDLE_TIMEOUT;

  fprintf(stderr,"recent_bundle_count now %d\n",recent_bundle_count);
  return 0;
}

int sync_is_bundle_recently_received(char *bid_prefix, long long version)
{
  for(int i=0;i<recent_bundle_count;i++) {
    
    if (!strcasecmp(bid_prefix,recent_bundles[i].bid_prefix)) {
      if (version<=recent_bundles[i].bundle_version)
	if (recent_bundles[i].timeout>=time(0)) {
	  printf("Ignoring %s*/%lld because we recently received %s*/%lld\n",
		 bid_prefix,version,
		 recent_bundles[i].bid_prefix,
		 recent_bundles[i].bundle_version);
	  return 1;
	} else
	  return 0;
      else return 0;
    }
  }
  return 0;  
}
