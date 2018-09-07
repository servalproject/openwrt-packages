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

extern int debug_sync_keys;
extern char *my_sid_hex;

#include "sync.h"
#include "lbard.h"

#define MAX_SENDERS 16384
char *senders[MAX_SENDERS];
char *sender_names[MAX_SENDERS];
int sender_count=0;

char *find_sender_name(char *sender)
{
  // XXX - Linear search! Will be very slow with many senders.
  // XXX - Replace with more efficient search strategy.
  for(int i=0;i<sender_count;i++)
    if (!strcasecmp(sender,senders[i]))
      return sender_names[i];
  return "";
}

int register_sender(char *sender,char *feedname)
{
  // XXX - See above: Replace with more efficiently searchable list format.
  int i;
  for(i=0;i<sender_count;i++) {
    if (!strcasecmp(sender,senders[i])) {
      // It is this sender
      if (strcmp(sender_names[i],feedname)) {
	free(sender_names[i]); sender_names[i]=strdup(feedname);
      }
      return 0;
    }
  }
  if (i>=MAX_SENDERS) {
    // Too many senders.  Random replace.
    i=random()%MAX_SENDERS;
    free(senders[i]); free(sender_names[i]);
    senders[i]=strdup(sender);
    sender_names[i]=strdup(feedname);
    return 0;
  }

  senders[sender_count]=strdup(sender);
  sender_names[sender_count++]=strdup(feedname);

  return 0;
}

int fresh_bundles[MAX_BUNDLES];
int fresh_bundle_count=0;
int note_new_or_updated_bundle(int bundle_number)
{
  if (fresh_bundle_count<MAX_BUNDLES) {
    fresh_bundles[fresh_bundle_count++]=bundle_number;
    return 0;
  }
  return 1;
}

struct bundle_record bundles[MAX_BUNDLES];
int bundle_count=0;
int ignored_bundles=0;

int register_bundle(char *service,
		    char *bid,
		    char *version,
		    char *author,
		    char *originated_here,
		    long long length,
		    char *filehash,
		    char *sender,
		    char *recipient,
		    char *name)
{
  int i;

  // XXX - Find and store feed name (= subscriber public name) for displaying when
  // explaining a MeshMS or MeshMB transfer on :21506 status page.
  
  // Calculate the key required for the bundle tree used to efficiently determine which
  // bundles a pair of peers have in common, and thus also the bundles each needs to
  // send to the other.
  sync_key_t bundle_sync_key;
  uint8_t bundle_tree_salt[SYNC_SALT_LEN]={0xa9,0x1b,0x8d,0x11,0xdd,0xee,0x20,0xd0};
  
  bundle_calculate_tree_key(&bundle_sync_key,bundle_tree_salt,
			    bid,strtoll(version,NULL,10),length,filehash);   

  if (debug_bundles)
    printf(">>> %s We now have bundle %s*,"
	   " service=%s, version=%s,"
	   " sender=%s,"
	   " recipient=%s, feedname=%s\n",
	   timestamp_str(),
	   bid,service,version,sender,recipient,name);

  if ((!strcmp(service,"MeshMB1"))&&name&&name[0]&&sender&&sender[0]) {
    register_sender(sender,name);
  }
  
  // Is it the OTA bundle?
  if (otabid&&(!strcasecmp(bid,otabid))) {
    printf(">>>>>> OTA bundle spotted.\n");
    process_ota_bundle(bid,version);
  }
  
  // Ignore non-meshms bundles when in meshms-only mode.
  // (actually, accepts both meshms (SMS-like service) and meshmb (micro-blogging service)
  if (meshms_only) {
    if (strncasecmp("meshm",service,5)) {
      rhizome_log(service,bid,version,author,originated_here,length,filehash,sender,recipient,
		  "Rejected non-meshms bundle seen while meshms_only=1");
      ignored_bundles++;
      return 0;
    }
  }
  
  long long versionll=strtoll(version,NULL,10);

  // Ignore bundles that are too old
  // (except if MeshMS2, since that uses journal bundles, and so the version does
  // not represent the age of a bundle.)
  if ((versionll<min_version)&&strncasecmp("meshms2",service,7)) {
    rhizome_log(service,bid,version,author,originated_here,length,filehash,sender,recipient,
		"Rejected bundle because it was too old (version<min_version), and service!=meshms2");
    ignored_bundles++;
    return 0;
  }
  
  // Remove bundle from partial lists of all peers if we have other transmissions
  // to us in progress of this bundle
  // XXX - Linear searches! Replace with hash table etc
  for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
    if (partials[i].bid_prefix) {
      // Here is a bundle in flight
      char *bid_prefix=partials[i].bid_prefix;
      long long bid_version=partials[i].bundle_version;
      
      if (versionll>=bid_version)
	if (!strncasecmp(bid,bid_prefix,strlen(bid_prefix)))
	  {
	    fprintf(stderr,"--- Culling in-progress transfer for bundle that has shown up in Rhizome.\n");
	    clear_partial(&partials[i]);
	    break;
	  }
    }
  }
  
  // XXX - Linear search through bundles!
  // Use a hash table or something so that it doesn't cost O(n^2) with number
  // of bundles.
  int bundle_number=bundle_count;
  for(i=0;i<bundle_count;i++) {
    if (!strcmp(bundles[i].bid_hex,bid)) {
      // Updating an existing bundle
      bundle_number=i; break;
    }
  }

  if (bundle_number>=MAX_BUNDLES) return -1;
  
  if (bundle_number<bundle_count) {
    // Replace old bundle values, ...

    // ... unless we already hold a newer version
    if (bundles[bundle_number].version>=versionll) {
      ignored_bundles++;
      return 0;
    }
    
    free(bundles[bundle_number].service);
    bundles[bundle_number].service=NULL;
    free(bundles[bundle_number].author);
    bundles[bundle_number].author=NULL;
    free(bundles[bundle_number].filehash);
    bundles[bundle_number].filehash=NULL;
    free(bundles[bundle_number].sender);
    bundles[bundle_number].sender=NULL;
    free(bundles[bundle_number].recipient);
    bundles[bundle_number].recipient=NULL;

    fprintf(stderr,">>> %s We have updated bundle %s/%lld\n",
	    timestamp_str(),bid,versionll);

  } else {    
    // New bundle
    bundles[bundle_number].bid_hex=strdup(bid);
    {
      for(int i=0;i<32;i++) {
	char hex[3]={bid[i*2+0],bid[i*2+1],0};
	bundles[bundle_number].bid_bin[i]=strtoll(hex,NULL,16);
      }
    }
    // Never announced
    bundles[bundle_number].last_offset_announced=0;
    bundles[bundle_number].last_version_of_manifest_announced=0;
    bundles[bundle_number].last_announced_time=0;
    bundle_count++;
    fprintf(stderr,">>> %s We have new bundle %s/%lld\n",
	    timestamp_str(),bid,versionll);

    // printf("There are now %d bundles.\n",bundle_count);
  }

  // Clear latest announcement time for bundles that get updated with a new version
  if (bundles[bundle_number].version<strtoll(version,NULL,10)) {
    bundles[bundle_number].last_offset_announced=0;
    bundles[bundle_number].last_version_of_manifest_announced=0;
    bundles[bundle_number].last_announced_time=0;
  }
  
  bundles[bundle_number].service=strdup(service);
  bundles[bundle_number].version=strtoll(version,NULL,10);
  bundles[bundle_number].author=strdup(author);
  bundles[bundle_number].originated_here_p=atoi(originated_here);
  bundles[bundle_number].length=length;
  bundles[bundle_number].filehash=strdup(filehash);
  bundles[bundle_number].sender=strdup(sender);
  bundles[bundle_number].recipient=strdup(recipient);
  bundles[bundle_number].sync_key=bundle_sync_key;
  
  bundles[bundle_number].index=bundle_number;
  
  // Add bundle to the sync tree 
  sync_add_key(sync_state,&bundle_sync_key,&bundles[bundle_number]);
  if (debug_sync_keys) {
    char filename[1024];
    snprintf(filename,1024,"lbardkeys.%s.has",my_sid_hex);
    FILE *f=fopen(filename,"a");
    fprintf(f,"%02X%02X%02X%02X%02X%02X%02X%02X:%s:%016llX\n",
	    bundle_sync_key.key[0],bundle_sync_key.key[1],
	    bundle_sync_key.key[2],bundle_sync_key.key[3],
	    bundle_sync_key.key[4],bundle_sync_key.key[5],
	    bundle_sync_key.key[6],bundle_sync_key.key[7],
	    bundles[bundle_number].bid_hex,bundles[bundle_number].version);
    fclose(f);
    
  }

  
  printf("  >> Inserted %s*/%lld into the tree: key=%02X%02X%02X (this is bundle #%d, now total of %d bundles, %d ignored)\n",
	 bundles[bundle_number].bid_hex,
	 bundles[bundle_number].version,
	 bundle_sync_key.key[0],
	 bundle_sync_key.key[1],
	 bundle_sync_key.key[2],
	 bundle_number,bundle_count,ignored_bundles);
  
  rhizome_log(service,bid,version,author,originated_here,length,filehash,sender,recipient,
	      "Bundle registered");

  // Add it to the list of bundles that have been added/updated,
  // for link types that need it (currently only Outernet uplink)
  note_new_or_updated_bundle(bundle_number); 
  
  // Now work out if the bundle is our over-the-air update bundle.
  // If so, then download the bundle to disk, and mark it for update
  
  return 0;
}

int we_have_this_bundle_or_newer(char *bid_prefix, long long version)
{
  int i;
  for(i=0;i<bundle_count;i++) {
    if (!strncasecmp(bundles[i].bid_hex,bid_prefix,strlen(bid_prefix))) {
      // We have this bundle, but do we have this version?
      if (bundles[i].version>=version) {
	// Ok, we have this already
	return 1;
	break;
      }
    }
  }
  return 0;
}

// Lookup recipient of BID from the bundles we have.
// (Used when working out if a bundle a peer has is addressed to a peer,
// because BARs don't contain the recipient (maybe they should contain a prefix?)
// and so we see if we have an older version of a bundle in our store, and if so,
// then use the recipient from there.
char *bundle_recipient_if_known(char *bid_prefix)
{
  int i;
  for(i=0;i<bundle_count;i++) {
    if (!strncasecmp(bundles[i].bid_hex,bid_prefix,strlen(bid_prefix))) {
      return bundles[i].recipient;
    }
  }

  return NULL;
}
  
