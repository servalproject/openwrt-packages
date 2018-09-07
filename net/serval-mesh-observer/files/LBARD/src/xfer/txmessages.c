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
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "sync.h"
#include "lbard.h"

int log2ish(int value)
{
  int result=0;
  while(value) {
    result++; value=value>>1;
  }
  return result;
}

long long size_byte_to_length(unsigned char size_byte)
{
  return 1<<size_byte;
}


int append_bytes(int *offset,int mtu,unsigned char *msg_out,
		 unsigned char *data,int count)
{
  if (((*offset)+count)<=mtu) {
    bcopy(data,&msg_out[*offset],count);
    (*offset)+=count;
    return 0;
  } else {
    printf("append_bytes(%d,%d,msg,data,%d) failed -- message too long by %d bytes.\n",
	    *offset,mtu,count,
	    (((*offset)+count)-mtu));
    return -1;
  }
}

/* Our time stratum. 0xff00 means that we have not had any external time input,
   other values are hops from a time authority (who may or may not actually be
   accurate).  The time is used for logging purposes, and while more accurate would
   be better, having the time to within a few seconds is a huge step up from every
   Mesh Extender thinking it is January 1970.
*/
int my_time_stratum=0xff00;

int message_counter=0;
int update_my_message(int serialfd,
		      unsigned char *my_sid, char *my_sid_hex,
		      int mtu,unsigned char *msg_out,
		      char *servald_server,char *credential)
{
#ifdef SYNC_BY_BAR
  /* There are a few possible options here.
     1. We have no peers. In which case, there is little point doing anything.
        EXCEPT that some people might be able to hear us, even though we can't
	hear them.  So we should walk through a prioritised ordering of some subset
	of bundles, presenting them in turn via the interface.
     2. We have peers, but we have no content addressed to them, that we have not
        already communicated to them.
        In which case, we act as for (1) above.
     3. We have peers, and have bundles addressed to one or more of them, and have
        not yet heard from those peers that they already have those bundles. In which
        case we should walk through presenting those bundles repeatedly until the
        peers acknowledge the receipt of those bundles.

	Thus we need to keep track of the bundles that other peers have, and that have
	been announced to us.

	We also need to take care to announce MeshMS bundles, and especially new and
	updated MeshMS bundles that are addressed to our peers so that MeshMS has
	minimal latency over the transport.  In other words, we don't want to have to
	wait hours for the MeshMS bundle in question to get announced.

	Thus we need some sense of "last announcement time" for bundles so that we can
	prioritise them.  This should be kept with the bundle record.  Then we can
	simply lookup the highest priority bundle, see where we got to in announcing
	it, and announce the next piece of it.

	We should probably also announce a BAR or two, so that peers know if we have
	received bundles that they are currently sending.  Similarly, if we see a BAR
	from a peer for a bundle that they have already received, we should reduce the
	priority of sending that bundle, in particular if the bundle is addressed to
	them, i.e., we have confirmation that the recipient has received the bundle.

	Finally, we should use network coding so that recipients can miss some messages
	without terribly upsetting the whole thing, unless the transport is known to be
	reliable.
  */
#else
  /*
    With sync tree method, we don't send anything other than a "here we are and this
    is my root tree hash" by default. Only when we register a peer do we start trying
    to talk further. We limit transmissions to syncing trees, and acknowledging frames,
    unless there are bundles which we have identified need to be sent to them.
  */
#endif

  // Build output message

  if (mtu<64) return -1;
  
  // Clear message
  bzero(msg_out,mtu);

  // Put prefix of our SID in first 6 bytes.
  for(int i=0;i<6;i++) { msg_out[i]=my_sid[i]; }

  // Put 2-byte message counter.
  // lower 15 bits is message counter.
  // the 16th bit indicates if this message is a retransmission
  // (always clear when constructing the message).
  msg_out[6]=message_counter&0xff;
  msg_out[7]=(message_counter>>8)&0x7f;

  int offset=8;

  if (!(random()%10)) {
    // Occassionally announce our time

    append_timestamp(msg_out,&offset);
  }
  if (!(random()%10)) {
    // Occassionally announce our instance (generation) ID
    append_generationid(msg_out,&offset);
  }
  
#ifdef SYNC_BY_BAR
  // Put one or more BARs
  int bar_number=find_highest_priority_bar();
  if (bundle_count&&((mtu-offset)>=BAR_LENGTH)) {
    append_bar(bar_number,&offset,mtu,msg_out);
  }

  // Request peers to send something interesting if they are not already
  request_wanted_content_from_peers(&offset,mtu,msg_out);

  // Fill up spare space with BARs
  int bar_count=0;
  while (bundle_count&&(mtu-offset)>=BAR_LENGTH) {
    int bundle_number=find_highest_priority_bar();
    append_bar(bundle_number,&offset,mtu,msg_out);
    bar_count++;
  }
  if (debug_announce) printf("bar_count=%d\n",bar_count);
#else
  /* Sync by tree.
     Ask for retransmissions as required, and otherwise participate in
     synchronisation process.  Also send relevant content based on what we
     know from the sync process.
     Basically we need to iterate through the peers and pick who to respond to.
     We also need the sequence numbers to be recipient specific.
  */
  sync_by_tree_stuff_packet(&offset,mtu,msg_out,
			    my_sid_hex,servald_server,credential);
#endif

  // Increment message counter
  message_counter++;

  if (0) { 
    printf("This message (hex): ");
    for(int i=0;i<offset;i++) printf("%02x",msg_out[i]);
    printf("\n");
  }

  if (radio_send_message(serialfd,msg_out,offset))
    fprintf(stderr,"radio_send_message() failed to send message.  This is bad, as report_queue entries may be lost forever.\n");

  return offset;
}
