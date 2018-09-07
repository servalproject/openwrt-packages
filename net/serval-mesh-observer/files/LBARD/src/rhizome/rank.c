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
#include <strings.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <assert.h>

#include "sync.h"
#include "lbard.h"

extern struct bundle_record bundles[MAX_BUNDLES];
extern int bundle_count;

#ifdef SYNC_BY_BAR
int peer_has_this_bundle_or_newer(int peer,char *bid_or_bidprefix, long long version)
{
  // XXX - Another horrible linear search!
  // XXX - Bundle lists need to be stored in a hash table or similar.

  int bundle;
  for(bundle=0;bundle<peer_records[peer]->bundle_count;bundle++)
    {
      // Check version first, because it is faster.
      if (version<=peer_records[peer]->versions[bundle])
	if (!strncasecmp(bid_or_bidprefix,peer_records[peer]->bid_prefixes[bundle],
			 strlen(peer_records[peer]->bid_prefixes[bundle])))
	  return 1;
    }
  
  return 0;
}
#endif

// Bigger values in this list means that factor is more important in selecting
// which bundle to announce next.
// Sent less recently is less important than size or whether it is meshms.
// This ensures that we rotate through the bundles we have.
// If a peer comes along who doesn't have a smaller or meshms bundle, then
// the less-peers-have-it priority will kick in.

// Size-based priority is a value between 0 - 0x3FF
// Number of peers who lack a bundle is added to this to boost priority a little
// for bundles which are wanted by more peers
#define BUNDLE_PRIORITY_SENT_LESS_RECENTLY    0x00000400
#define BUNDLE_PRIORITY_RECIPIENT_IS_A_PEER   0x00002000
#define BUNDLE_PRIORITY_IS_MESHMS             0x00004000
// Transmit now only gently escalates priority, so that we can override it if
// we think we have a bundle that they should receive
#define BUNDLE_PRIORITY_TRANSMIT_NOW          0x00000040
// HAving a priority flag for bundles we don't have
// We want this big, but not too big, so that we can
// easily de-prioritise bundles we don't have, if they
// fail to insert into rhizome a few times
#define BUNDLE_PRIORITY_WE_DONT_HAVE_IT       0x00010000
// Two failed inserts negates the "we don't have it" priority.
#define BUNDLE_PRIORITY_PENALTY_FOR_FAILED_INSERT 0x8000


int lengthToPriority(long long value)
{
  long long original_value=value;
  
  int result=0;
  while(value) {
    result++; value=value>>1;
  }

  int shift=4;
  if (result<=shift) shift=result-1;
  long long part=((original_value-(1<<result))>>(result-shift))&0xf;

  part=15-part;
  result=63-result;
  
  return (result<<4)|part;
}

long long calculate_bundle_intrinsic_priority(char *bid,
					      long long length,
					      long long version,
					      char *service,
					      char *recipient,
					      int insert_failures)
{

  // Allow disabling of bundle prioritisation for comparison of effect
  // of prioritisation 
  if (debug_noprioritisation) {
     printf("WARNING: Rhizome bundle prioritisation disabled.\n");
     return 1;
  }

  // Start with length
  long long this_bundle_priority = lengthToPriority(length);

  // Prioritise MeshMS over others (and new style MeshMS over any old meshms v1
  // messages).
  if (!strcasecmp("MeshMS1",service))
    this_bundle_priority+=BUNDLE_PRIORITY_IS_MESHMS;
  if (!strcasecmp("MeshMS2",service))
    this_bundle_priority+=2*BUNDLE_PRIORITY_IS_MESHMS;
  
  // Is bundle addressed to a peer?
  int j;
  int addressed_to_peer=0;
  if (recipient) {
    for(j=0;j<peer_count;j++) {
      if (!strncmp(recipient,
		   peer_records[j]->sid_prefix,
		   (8*2))) {
	// Bundle is addressed to a peer.
	// Increase priority if we do not have positive confirmation that peer
	// has this version of this bundle.
	addressed_to_peer=1;

#ifdef SYNC_BY_BAR
	int k;
	for(k=0;k<peer_records[j]->bundle_count;k++) {
	  if (!strncmp(peer_records[j]->bid_prefixes[k],bid,
		       8*2)) {
	    // Peer knows about this bundle, but which version?
	    if (peer_records[j]->versions[k]<version) {
	      // They only know about an older version.
	      // XXX Advance bundle announced offset to last known offset for
	      // journal bundles (MeshMS1 & MeshMS2 types, and possibly others)
	    } else {
	      // The peer has this version (or possibly a newer version!), so there
	      // is no point us announcing it.
	      addressed_to_peer=0;
	    }
	  }
	}
#endif
	
	// We found who this bundle was addressed to, so there is no point looking
	// further.
	break;
      }
    }
  }
  if (addressed_to_peer)
    this_bundle_priority+=BUNDLE_PRIORITY_RECIPIENT_IS_A_PEER;

  // Halve priority for every time we have failed to insert the complete thing in
  // rhizome.
  for(int i=0;i<insert_failures;i++)
    if (this_bundle_priority>=BUNDLE_PRIORITY_PENALTY_FOR_FAILED_INSERT)
	this_bundle_priority-=BUNDLE_PRIORITY_PENALTY_FOR_FAILED_INSERT;
    else {
      // There isn't enough priority left to penalise again, so we will
      // instead halve the remaining priority for each extra fail.
      this_bundle_priority=this_bundle_priority>>1;
    }

  return this_bundle_priority;
}

int calculate_stored_bundle_priority(int i,int versus)
{    
  // Allow disabling of bundle prioritisation for comparison of effect
  // of prioritisation 
  if (debug_noprioritisation) {
     printf("WARNING: Rhizome bundle prioritisation disabled.\n");
     return 1;
  }

  // Start with intrinsic priority of the bundle based on size, service,
  // who it is addressed to, and whether we have had problems inserting it
  // into rhizome.
  long long this_bundle_priority=
    calculate_bundle_intrinsic_priority(bundles[i].bid_hex,
					bundles[i].length,
					bundles[i].version,
					bundles[i].service,
					bundles[i].recipient,
					0 /* it is a bundle in rhizome, so
					     insert_failures is meaningless here. */
					);
  
  long long time_delta=0;
  
  if (versus>=0) {
    time_delta=bundles[versus].last_announced_time
      -bundles[i].last_announced_time;
    
  } else time_delta=0;

#ifdef SYNC_BY_BAR
  if (bundles[i].transmit_now)
    if (bundles[i].transmit_now>=time(0)) {
      this_bundle_priority+=BUNDLE_PRIORITY_TRANSMIT_NOW;
    }
#endif
  
  int num_peers_that_dont_have_it=0;
#ifdef SYNC_BY_BAR
  int peer;
  time_t peer_observation_time_cutoff=time(0)-peer_keepalive_interval;
  for(peer=0;peer<peer_count;peer++) {
    if (peer_records[peer]->last_message_time>=peer_observation_time_cutoff)
      if (!peer_has_this_bundle_or_newer(peer,
					 bundles[i].bid,
					 bundles[i].version))
	num_peers_that_dont_have_it++;
  }
#endif
  
  // We only apply the less-recently-sent priority flag if there are peers who
  // don't yet have it.
  // XXX - This is still a bit troublesome, because we may not have announced the
  // bar, and the segment headers don't have enough information for the far end
  // to start actively requesting the bundle. To solve this, we should provide the
  // BAR of a bundle at least some of the time when presenting pieces of it.
  
  // Actually, this is really just a pain all round. We need it so that we sequence
  // through all bundles. But any bundle that has peers who don't have it, then we
  // should not allow other things to be advanced ahead of the bunch that includes
  // this one.  So we should probably just ignore time_delta if peers need this one.
  if ((time_delta>=0LL)||num_peers_that_dont_have_it) {      
    this_bundle_priority+=BUNDLE_PRIORITY_SENT_LESS_RECENTLY;
  }
  
  if (0)
    fprintf(stderr,"  bundle %s was last announced %ld seconds ago.  "
	    "Priority = 0x%llx, %d peers don't have it.\n",
	    bundles[i].bid_hex,time(0)-bundles[i].last_announced_time,
	    this_bundle_priority,num_peers_that_dont_have_it);
  
  // Add to priority according to the number of peers that don't have the bundle
  this_bundle_priority+=num_peers_that_dont_have_it;
  if (num_peers_that_dont_have_it>bundles[i].num_peers_that_dont_have_it) {
    // More peer(s) have arrived who have not got this bundle yet, so reset the
    // last sent time for this bundle.
    bundles[i].last_announced_time=0;
  }
  bundles[i].num_peers_that_dont_have_it=num_peers_that_dont_have_it;

  // Remember last calculated priority so that we can help debug problems with
  // priority calculation.
  bundles[i].last_priority=this_bundle_priority;
  
  return this_bundle_priority;
}

int find_highest_priority_bundle()
{
  long long this_bundle_priority=0;
  long long highest_bundle_priority=0;
  int i;
  int highest_priority_bundle=-1;
  //  int highest_priority_bundle_peers_dont_have_it=0;

  for(i=0;i<bundle_count;i++) {

    this_bundle_priority = calculate_stored_bundle_priority(i,highest_priority_bundle);
    
    // Indicate this bundle as highest priority, unless we have found another one that
    // is higher priority.
    // Replace if priority is equal, so that newer bundles take priorty over older
    // ones.
    {
      if ((i==0)||(this_bundle_priority>highest_bundle_priority)) {
	if (0) fprintf(stderr,"  bundle %d is higher priority than bundle %d"
		       " (%08llx vs %08llx)\n",
		       i,highest_priority_bundle,
		       this_bundle_priority,highest_bundle_priority);
	highest_bundle_priority=this_bundle_priority;
	highest_priority_bundle=i;
	// highest_priority_bundle_peers_dont_have_it=bundles[i].num_peers_that_dont_have_it;
      }
    }    
  }
  
  return highest_priority_bundle;
}

#ifdef SYNC_BY_BAR
int bundle_bar_counter=0;
int find_highest_priority_bar()
{
  int bar_number;
  // XXX This can probably get stuck in a loop announcing the same
  // BAR over and over in an attempt to stop another node announcing
  // segments of a bundle to a different peer who doesn't have it yet.
  // So for now we have the crude mechanism of only making such announcements
  // 1/2 the time, so that we can keep announcing new BARs to our peers.
  if (random()&1) {
    for(bar_number=0;bar_number<bundle_count;bar_number++) 
      if (bundles[bar_number].announce_bar_now) {
	bundles[bar_number].announce_bar_now=0;
	return bar_number;
      }
  }
  
  bundle_bar_counter++;
  if (bundle_bar_counter>=bundle_count) bundle_bar_counter=0;
  return bundle_bar_counter;
}
#endif
