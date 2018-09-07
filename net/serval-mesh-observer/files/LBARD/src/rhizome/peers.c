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

#include "sync.h"
#include "lbard.h"


int free_peer(struct peer_state *p)
{
  if (p->sid_prefix) free(p->sid_prefix); p->sid_prefix=NULL;
  for(int i=0;i<4;i++) p->sid_prefix_bin[i]=0;
#ifdef SYNC_BY_BAR
  for(int i=0;i<p->bundle_count;i++) {
    if (p->bid_prefixes[i]) free(p->bid_prefixes[i]);    
  }
  free(p->bid_prefixes); p->bid_prefixes=NULL;
  free(p->versions); p->versions=NULL;
  free(p->size_bytes); p->size_bytes=NULL;
  free(p->insert_failures); p->insert_failures=NULL;
#endif
  sync_free_peer_state(sync_state, p);
  free(p);
  return 0;
}

#ifdef SYNC_BY_BAR
int peer_note_bar(struct peer_state *p,
		  char *bid_prefix,long long version, char *recipient_prefix,
		  int size_byte)
{
  int b=-1;

  // Ignore bundles that are too old
  if (version<min_version) return 0;

  if (0) {
    for(int i=0;i<p->bundle_count;i++)
      printf("  bundle #%d/%d: %s* version %lld\n",
	      i,p->bundle_count,
	      p&&p->bid_prefixes&&p->bid_prefixes[i]?p->bid_prefixes[i]:"<bad>",
	      p&&p->versions&&p->versions[i]?p->versions[i]:-1);
    printf("  bundle list end.\n");
  }
  
  // XXX Argh! Another linear search! Replace with something civilised
  for(int i=0;i<p->bundle_count;i++)
    if (!strcmp(p->bid_prefixes[i],bid_prefix)) {
      b=i;
      if (0) printf("Peer %s* has bundle %s* version %lld (we already knew that they have version %lld)\n",
		     p->sid_prefix,bid_prefix,version,p->versions[b]);
      break;
    }
  if (b==-1) {
    // New bundle.
    if (0) printf("Peer %s* has bundle %s* version %lld, which we haven't seen before\n",
	    p->sid_prefix,bid_prefix,version);
    if (p->bundle_count>=MAX_PEER_BUNDLES) {
      // BID list too full -- random replacement.
      b=random()%p->bundle_count;
      free(p->bid_prefixes[b]); p->bid_prefixes[b]=NULL;
    }
    if (p->bundle_count>=p->bundle_count_alloc) {
      // Allocate new list space
      p->bundle_count_alloc+=1000;
      p->bid_prefixes=realloc(p->bid_prefixes,sizeof(char *)*p->bundle_count_alloc);
      assert(p->bid_prefixes);
      p->versions=realloc(p->versions,sizeof(long long)*p->bundle_count_alloc);
      assert(p->versions);
      p->size_bytes=realloc(p->size_bytes,p->bundle_count_alloc);
      assert(p->size_bytes);
      p->insert_failures=realloc(p->insert_failures,p->bundle_count_alloc);
      assert(p->insert_failures);
    }
    b=p->bundle_count;
    if (debug_pieces) printf("Peer %s* bundle %s* will go in index %d (current count = %d)\n",
	    p->sid_prefix,bid_prefix,b,p->bundle_count);
    p->bid_prefixes[b]=strdup(bid_prefix);
    if (b>=p->bundle_count) p->bundle_count++;
  }
  assert(p);
  p->versions[b]=version;
  p->size_bytes[b]=size_byte;
  p->insert_failures[b]=0;
  
  return 0;
}
#endif


struct peer_state *peer_records[MAX_PEERS];
int peer_count=0;

int find_peer_by_prefix(char *peer_prefix)
{
  for(int i=0;i<peer_count;i++)
    if (!strcasecmp(peer_records[i]->sid_prefix,peer_prefix))
      return i;
  
  return -1;
}

#ifdef SYNC_BY_BAR
// The most interesting bundle a peer has is the smallest MeshMS bundle, if any, or
// else the smallest bundle that it has, but that we do not have.
// XXX - We should take into account neighbours we can see to prioritise content for
// them (and neighbours of neighbours, since phones are usually not going to be running
// lbard).
// XXX - Actually, we can't even take into account MeshMS or size, because we currenlty
// only store version and BID prefix. version will do as a decent proxy for these
// things for now, since for meshms version is the journal bundle length.
// XXX - We should also not ask for bundles we are currently transferring from other
// peers. This is tricky to enforce, because there are race conditions that could
// easily end up with this exact scenario.
int peers_most_interesting_bundle(int peer)
{
  int best_bundle=-1;
  long long best_priority=-1;
  int bundle;

  // XXX - More linear searches!
  for (bundle=0;bundle<peer_records[peer]->bundle_count;bundle++) {
    int interesting=1;
    if (!peer_records[peer]->bid_prefixes[bundle]) interesting=0;
    else {
      if (we_have_this_bundle_or_newer(peer_records[peer]->bid_prefixes[bundle],
				       peer_records[peer]->versions[bundle]))
	interesting=0;
    }
    if (interesting) {
      // Get everything that we need handy to work out the intrinsic priority of
      // this bundle.
      char *bid = peer_records[peer]->bid_prefixes[bundle];
      long long size_estimate
	= size_byte_to_length(peer_records[peer]->size_bytes[bundle]&0x7f);
      long long version = peer_records[peer]->versions[bundle];
      char *service = "file";
      if (peer_records[peer]->size_bytes[bundle]&0x80) service="MeshMS2";
      char *recipient = bundle_recipient_if_known(bid);
      int insert_failures = peer_records[peer]->insert_failures[bundle];

      long long this_priority =
	calculate_bundle_intrinsic_priority(bid,size_estimate,version,service,
					    recipient,insert_failures);
      
      // Bundle is more interesting if it is smaller or meshms and the best so far is
      // not. We do this by comparing size_bytes first, which will put all meshms to
      // the front of the queue, and then version, which will allow finer
      // discrimination of size differences if size_bytes are identical.

      if ((best_bundle==-1)||(this_priority>best_priority))
	{
	  best_bundle=bundle;
	  best_priority=this_priority;
	}
    }
  }
  
  return best_bundle;
}
#endif

int hex_to_val(int c)
{
  if (c>='0'&&c<='9') return c-'0';
  if (c>='A'&&c<='F') return c-'A'+10;
  if (c>='a'&&c<='f') return c-'a'+10;
  return 0;
}


int last_peer_requested=0;

int random_active_peer()
{
  int peer;
  int the_peer=-1;

  // Work out who to ask next?
  // (consider all peers in round-robin)
  peer=last_peer_requested+1;
  if (peer>=peer_count) peer=0;
  if (peer<0) peer=0;
  
  for(;peer<peer_count;peer++)
    {
      if (!peer_records[peer]) continue;
      if ((time(0)-peer_records[peer]->last_message_time)>peer_keepalive_interval) {
	continue;
      }
      the_peer=peer;
      break;
    }
  if (the_peer==-1)
    for(peer=0;(peer<=last_peer_requested)&&(peer<peer_count);peer++)
      {
	if (!peer_records[peer]) continue;
	if ((time(0)-peer_records[peer]->last_message_time)>peer_keepalive_interval) {
	  continue;
	}
	the_peer=peer;
	break;
      }

#if 0  
  char active_peers[1024];
  int apl=0;
  for(peer=0;(peer<peer_count);peer++)
    {
      if (!peer_records[peer]) continue;
      if ((time(0)-peer_records[peer]->last_message_time)>peer_keepalive_interval)
	continue;
      snprintf(&active_peers[apl],1024-apl,"%d, ",peer);
      apl=strlen(active_peers);
      printf("     active_peers='%s'\n",active_peers);
    }
  if (apl>1) active_peers[apl-2]=0;
  else strcpy(active_peers,"<none>");
  printf("T+%lldms : Random peer is %d (active peer(s) = %s, last requested = %d)\n",gettime_ms()-start_time,peer,active_peers,last_peer_requested);
#endif
  
  peer=the_peer;
  if (peer>=-1) last_peer_requested=peer;
  
  return peer;
}

int active_peer_count()
{
  int count=0;
  for(int peer=0;peer<peer_count;peer++)
    if ((time(0)-peer_records[peer]->last_message_time)<=peer_keepalive_interval)
      count++;
  return count;
}


#ifdef SYNC_BY_BAR
int request_wanted_content_from_peers(int *offset,int mtu, unsigned char *msg_out)
{
  int peer;

  // Work out who to ask next?
  // (consider all peers in round-robin)
  if (last_peer_requested>=peer_count) last_peer_requested=0;
  peer=last_peer_requested;
  last_peer_requested++;
  
  for(;peer<peer_count;peer++)
    {
      // Keep track of what we are getting from this peer, and try to finish
      // the most complete things first.
      // XXX - It would really help if we knew in advance the length of a payload
      // so that we could actually do this properly.
      // XXX - Instead for now, we just request the first missing thing we have.
      // XXX - We now have access to size_bytes from BARs to help us prioritise
      // content.
      // int most_complete_partial=-1;
      // int most_complete_remaining=-1;
      // int most_complete_start=-1;
      // int most_complete_manifest_or_body=-1;

      // Don't request anything from a peer that we haven't heard from for a while
      if ((time(0)-peer_records[peer]->last_message_time)>peer_keepalive_interval)
	continue;

      // If we got here, the peer is not currently sending us anything interesting.
      // So have a look at what the peer has to offer, and ask for something
      // interesting.
      int most_interesting_bundle=peers_most_interesting_bundle(peer);
      if (most_interesting_bundle>-1) {
	// Peer has something interesting.

	// Now see if it is being transferred to us already.
	// If so, request the most useful segment of it.
	int i;
	for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
	  if (peer_records[peer]->partials[i].bid_prefix) {
	    if (!strcasecmp(peer_records[peer]->partials[i].bid_prefix,
			    peer_records[peer]->bid_prefixes[most_interesting_bundle]))
	      {
		// This bundle is in flight.
		// We are receiving something from this peer, so we presume it is
		// interesting.
		// Our approach to requesting missing parts is simple:
		// 1. Request missing stuff from the start, if any.
		// 2. Else, request from the end of the first segment, so that we will tend
		// to merge segments.
		struct segment_list *s=peer_records[peer]->partials[i].manifest_segments;
		while(s&&s->next) s=s->next;
		if ((!s)||(s->start_offset||(s->length<peer_records[peer]->partials[i].manifest_length)||peer_records[peer]->partials[i].manifest_length<0))
		  {
		    if (debug_pull) {
		      printf("We need manifest bytes...\n");
		      dump_segment_list(peer_records[peer]->partials[i].manifest_segments);
		    }
		    if ((!s)||s->start_offset) {
		      // We are missing bytes at the beginning
		      return request_segment(peer,
					     peer_records[peer]->partials[i].bid_prefix,
					     peer_records[peer]->partials[i].body_length,
					     0,1 /* manifest */,offset,mtu,msg_out);
		    } else if (s) {
		      if (debug_pull) {
			printf("We need manifest bytes...\n");
			dump_segment_list(peer_records[peer]->partials[i].manifest_segments);
		      }
		      return request_segment(peer,
					     peer_records[peer]->partials[i].bid_prefix,
					     peer_records[peer]->partials[i].body_length,

					     (s->start_offset+s->length),
					     1 /* manifest */,offset,mtu,msg_out);
		    }
		  }
		s=peer_records[peer]->partials[i].body_segments;
		if (debug_pull) dump_segment_list(s);
		while(s&&s->next) s=s->next;
		if ((!s)||s->start_offset) {
		  // We are missing bytes at the beginning
		  if (debug_pull) {
		    printf("We need body bytes at the start (start_offset=%d)...\n",
			    s?s->start_offset:-1);
		    dump_segment_list(peer_records[peer]->partials[i].body_segments);
		  }
		  return request_segment(peer,
					 peer_records[peer]->partials[i].bid_prefix,
					 peer_records[peer]->partials[i].body_length,
					 0,
					 0 /* not manifest */,offset,mtu,msg_out);
		} else if (s) {
		  if (debug_pull) {
		    printf("We need body bytes @ %d...\n",
			    s->start_offset+s->length);
		    dump_segment_list(peer_records[peer]->partials[i].body_segments);
		  }
		  return request_segment(peer,
					 peer_records[peer]->partials[i].bid_prefix,
					 peer_records[peer]->partials[i].body_length,
					 (s->start_offset+s->length),
					 0 /* not manifest */,offset,mtu,msg_out);
		}		
	      }
	  }
	}

	// The most interesting bundle is not currently in flight, so request first piece of it.
	// XXX Don't do this now, as it just wastes a lot of bandwidth when nodes are
	// synchronising bundle lists.  It is better to just let the nodes exchange BARs until
	// they have something useful to transmit.
	if (0)
	  return request_segment(peer,peer_records[peer]->bid_prefixes[most_interesting_bundle],
			       -1,

			       0,0 /* not manifest */,offset,mtu,msg_out);
      }
      
    }
  return 0;
}

int bid_to_peer_bundle_index(int peer,char *bid_hex)
{
  // Find the bundle id of this bid in a peer's list of bundles
  if ((peer<0)||(peer>peer_count)) return -1;
  
  struct peer_state *p=peer_records[peer];
  
  for(int i=0;i<p->bundle_count;i++) {
    if (!strncasecmp(bid_hex,p->bid_prefixes[i],strlen(p->bid_prefixes[i])))
      return i;
  }
  return -1;
}
#endif

int peer_queue_list_dump(struct peer_state *p)
{
  printf("& TX QUEUE TO %s*\n",
	 p->sid_prefix);
  printf("& tx_bundle=%d, tx_bundle_bid=%s*, priority=%d\n",
	 p->tx_bundle,
	 (p->tx_bundle>-1)?
	 bundles[p->tx_bundle].bid_hex:"",
	 p->tx_bundle_priority);
  printf("& %d more queued\n",p->tx_queue_len);
  for(int i=0;i<p->tx_queue_len;i++) {
    int bundle=p->tx_queue_bundles[i];
    int priority=p->tx_queue_priorities[i];
    printf("  & bundle=%d, bid=%s*, priority=%d\n",	   
	   bundle,bundles[bundle].bid_hex,priority);

  }
  return 0;
}

int peer_queue_bundle_tx(struct peer_state *p,struct bundle_record *b, int priority)
{
  int i;
  int pn=-1;

  for(i=0;i<pn;i++) if (p==peer_records[i]) pn=i;

  printf("Queueing bundle #%d ",b->index);
  if (pn>-1)
    describe_bundle(RESOLVE_SIDS,stdout,NULL,b->index,pn,-1,-1);
  printf(" for transmission to %s*\n",p->sid_prefix);
  
  // Don't queue if already in the queue
  for(i=0;i<p->tx_queue_len;i++) 
    {
      if (p->tx_queue_bundles[i]==b->index) return 0;
    }
  
  // Find point of insertion
  for(i=0;i<p->tx_queue_len;i++) 
    if (p->tx_queue_priorities[i]<priority) { break; }

  if (i<MAX_TXQUEUE_LEN) {    
    // Shift rest of list down
    if (i<(p->tx_queue_len-1)) {
      bcopy(&p->tx_queue_priorities[i],
	    &p->tx_queue_priorities[i+1],
	    sizeof(int)*(p->tx_queue_len-i-1));
      bcopy(&p->tx_queue_bundles[i],
	    &p->tx_queue_bundles[i+1],
	    sizeof(int)*(p->tx_queue_len-i-1));
    }
    
    // Write new entry
    p->tx_queue_bundles[i]=b->index;
    p->tx_queue_priorities[i]=priority;
    if (i>=p->tx_queue_len)
      p->tx_queue_len=i+1;

    // printf("After queueing new bundle:\n"); fflush(stdout);
    // peer_queue_list_dump(p);
    
    return 0;
  } else {
    // Fail on insertion if the queue is already full of higher priority stuff.
    
    /* Remember that TX queue has overflowed, so that when the TX queue is 
       empitied, we know that we need to re-sync our tree with them to
       rediscover the bundles that should be sent.
       XXX - Eventually this will be replaced with Jeremy's new sync tree
       enumeration code, that will allow us to rediscover the missing bundles,
       without throwing away all sync state.
    */
    
    p->tx_queue_overflow=1;
    return -1;
  }
}
