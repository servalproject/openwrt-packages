/*
Serval Low-bandwidth asychronous Rhizome Demonstrator.
Copyright (C) 2018 Paul Gardner-Stephen

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
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <dirent.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "sync.h"
#include "lbard.h"
#include "serial.h"
#include "version.h"
#include "radios.h"
#include "code_instrumentation.h"

int outernet_socket=-1;

extern char *servald_server;
extern char *credential;


/*
  See also drv_outernet.c, and outernet_uplink_build_packet()
  in particular.

  The packets we send are protected by RAID-5 like parity,
  which can be used to reconstruct a single missing packet
  from each parity zone.  The initial implementation uses 
  a parity zone of four packets, with 1/4 of the data being
  parity, that is the parity expands the dataa to 4/3 the
  original size, and thus allows one in four packets to be
  lost, without loss of data.

  There are five lanes of transfer simultaneously, so we
  need to keep track of those.

  The packet format is:

  lane number - one byte
  sequence number + stop and start markers - two bytes
  logical MTU - one byte
  data - variable length
  parity stripe - variable length

  More specifically, the logical MTU specifies the total packet
  size being used, for the purposes of determining the data and
  parity strip sizes in each packet. 

*/

struct outernet_rx_bundle {
  unsigned int offset;
  unsigned int parity_zone_number;
  unsigned int last_parity_zone_number;
  unsigned int data_size;
  unsigned char *data;
#define MAX_DATA_BYTES 256
  unsigned char parity_zone[MAX_DATA_BYTES*4];
  unsigned char parity_bytes[MAX_DATA_BYTES];
  unsigned char parity_zone_bitmap;
  unsigned char data_bytes;  
  unsigned char waitingForStart;
};

#define MAX_LANES 5
struct outernet_rx_bundle outernet_rx_bundles[MAX_LANES];

int outernet_rx_lane_init(int i,int freeP);

int outernet_rx_try_bundle_insert(int lane)
{
  int retVal=0;
  LOG_ENTRY;

  do {

    LOG_NOTE("Trying to insert bundle from lane #%d",lane);
    
    if (!outernet_rx_bundles[lane].data) {      
      retVal=-1;
      break;
    }

    dump_bytes(stdout,"Received data",outernet_rx_bundles[lane].data,
	       1024);
    
    unsigned int packed_manifest_len=outernet_rx_bundles[lane].data[0]
      +(outernet_rx_bundles[lane].data[1]<<8);
    unsigned char manifest[8192];
    int manifest_len=8192;
    unsigned int payload_len=outernet_rx_bundles[lane].data[2]
      +(outernet_rx_bundles[lane].data[3]<<8)
      +(outernet_rx_bundles[lane].data[4]<<16)
      +(outernet_rx_bundles[lane].data[5]<<24);      
  
    LOG_NOTE("compress manifest length = %d bytes.",packed_manifest_len);
    dump_bytes(stdout,"Packed manifest",&outernet_rx_bundles[lane].data[2+4],packed_manifest_len);
    int r=manifest_binary_to_text(&outernet_rx_bundles[lane].data[2+4],packed_manifest_len,
				  manifest,&manifest_len);
    if (r) {
      LOG_ERROR("Failed to decompress binary manifest");
      retVal=-1;
      break;
    }
    LOG_NOTE("Manifest decompressed to %d bytes",manifest_len);
    dump_bytes(stdout,"Manifest",manifest,manifest_len);
    dump_bytes(stdout,"Payload",&outernet_rx_bundles[lane].data[2+4+packed_manifest_len],
	       payload_len);

    if ((2+4+payload_len)>outernet_rx_bundles[lane].data_size) {
      LOG_ERROR("Bundle is longer than what we have received");
      retVal=-1;
      break;
    }

    // Otherwise, insert the bundle into the rhizome database if we can
    r=rhizome_update_bundle(manifest,manifest_len,
			    &outernet_rx_bundles[lane].data[2+4+packed_manifest_len],payload_len,
			    servald_server,credential);
    LOG_NOTE("rhizome_update_bundle() returned %d",r);	     

    
  } while(0);
  
  LOG_EXIT;
  return retVal;
}


int outernet_rx_lane_init(int i,int freeP)
{
  /* Clear RX lane.
     If there was something in it, we should try to insert it as a bundle.
  */

  if (freeP) {
    // Try to register the bundle, in case we got the whole
    // thing, except for a missing packet at the end, and thus
    // didn't see the end flag.
    LOG_NOTE("Attempting to insert bundle received via outernet via lane_init()");
    outernet_rx_try_bundle_insert(i);
  }

  LOG_NOTE("Clearing lane #%d",i);
  outernet_rx_bundles[i].waitingForStart=1;
  outernet_rx_bundles[i].data_size=0;
  if (freeP&&outernet_rx_bundles[i].data) free(outernet_rx_bundles[i].data);
  outernet_rx_bundles[i].data=NULL;
  return 0;
}

int outernet_rx_lane_commit_parity_zone(int lane)
{
  int retVal=0;
  LOG_ENTRY;

  do {
    LOG_NOTE("Commiting parity zone at offset %d for lane #%d",
	     outernet_rx_bundles[lane].parity_zone_number
	     *4*outernet_rx_bundles[lane].data_bytes,lane);

    if ((!outernet_rx_bundles[lane].data)
	||(outernet_rx_bundles[lane].data_size
	   < ( outernet_rx_bundles[lane].parity_zone_number + 1)
	   * 4 * outernet_rx_bundles[lane].data_bytes))
      {
	// Insufficient space allocated, realloc.
	
	unsigned int new_size=outernet_rx_bundles[lane].data_size;
	if (!new_size) new_size=65536;
	else new_size=new_size<<1;
	if (new_size<outernet_rx_bundles[lane].data_size)
	  new_size=outernet_rx_bundles[lane].data_size;
	
	unsigned char *d=realloc(outernet_rx_bundles[lane].data,
				 (size_t)new_size);
	if (!d) {
	  LOG_ERROR("realloc(%u) failed",new_size);
	  // We can't allocate enough space, so free the lane
	  // for the next bundle, which will hopefully be smaller.
	  outernet_rx_lane_init(lane,1);
	  retVal=-1;
	  break;
	} else {
	  outernet_rx_bundles[lane].data=d;
	  outernet_rx_bundles[lane].data_size=new_size;	  
	}
      }
    
    // Copy the parity zone into place
    dump_bytes(stderr,"Parity zone in commit",
	       outernet_rx_bundles[lane].parity_zone,
	       4*outernet_rx_bundles[lane].data_bytes);	       
    memcpy(&outernet_rx_bundles[lane].data
	   [outernet_rx_bundles[lane].parity_zone_number
	    *4*outernet_rx_bundles[lane].data_bytes],
	   outernet_rx_bundles[lane].parity_zone,
	   4*outernet_rx_bundles[lane].data_bytes);
    
    // Prepare for receiving the next parity zone
    outernet_rx_bundles[lane].last_parity_zone_number=
      outernet_rx_bundles[lane].parity_zone_number;
    outernet_rx_bundles[lane].parity_zone_number++;
    outernet_rx_bundles[lane].parity_zone_bitmap=0;        
    
  } while(0);

  LOG_EXIT;
  return retVal;
}

int outernet_rx_lane_update_parity_zone(int lane)
{
  int retVal=0;
  LOG_ENTRY;

  do {
    unsigned char *pz=outernet_rx_bundles[lane].parity_zone;
    unsigned char *pb=outernet_rx_bundles[lane].parity_bytes;
    int data_bytes=(unsigned char)outernet_rx_bundles[lane].data_bytes;
    int parity_bytes=data_bytes/3;

    // Handy macros to make it easier to decode
#define PZ(A,B) (&pz[A*data_bytes+B*parity_bytes])
#define PB(A) (&pb[A*parity_bytes])
#define COPY(FROM,TO) { LOG_NOTE("COPY(%p -> %p) (data_bytes=%d)",FROM,TO,data_bytes); memcpy(TO,FROM,parity_bytes); }
#define XOR(THIS,ONTO) { for(int i=0;i<parity_bytes;i++) ONTO[i]^=THIS[i]; }
    
    switch (outernet_rx_bundles[lane].parity_zone_bitmap)
      {
      case 0x0: case 0x1: case 0x2: case 0x3:
      case 0x4: case 0x5: case 0x6: case 0x8:
      case 0x9: case 0xa: case 0xc:
	// Do nothing if we don't have enough pieces of this parity zone
	// Less than 3 pieces received from last parity zone
	retVal=-1;
	break;
      case 0x7:
	// Missing piece 3

	
	// Copy the parity slices to the missing spot
	COPY(PB(0),PZ(3,0));
	COPY(PB(1),PZ(3,1));
	COPY(PB(2),PZ(3,2));

	// Reveal the original data
	XOR(PZ(1,0),PZ(3,0)); XOR(PZ(2,0),PZ(3,0));
	XOR(PZ(0,0),PZ(3,1)); XOR(PZ(2,1),PZ(3,1));
	XOR(PZ(0,1),PZ(3,2)); XOR(PZ(1,1),PZ(3,2));

	outernet_rx_lane_commit_parity_zone(lane);
	
	break;	
      case 0xb:
	// missing piece 2

	// Copy the parity slices to the missing spot
	COPY(PB(0),PZ(2,0));
	COPY(PB(1),PZ(2,1));
	COPY(PB(2),PZ(2,2));

	// Reveal the original data
	XOR(PZ(1,0),PZ(2,0)); XOR(PZ(3,0),PZ(2,0));
	XOR(PZ(0,0),PZ(2,1)); XOR(PZ(3,1),PZ(2,1));
	XOR(PZ(0,2),PZ(2,2)); XOR(PZ(1,2),PZ(2,2));

	outernet_rx_lane_commit_parity_zone(lane);

	break;
      case 0xd:
	// missing piece 1

	// Copy the parity slices to the missing spot
	COPY(PB(0),PZ(1,0));
	COPY(PB(1),PZ(1,1));
	COPY(PB(2),PZ(1,2));

	// Reveal the original data
	XOR(PZ(2,0),PZ(1,0)); XOR(PZ(3,0),PZ(1,0));
	XOR(PZ(0,1),PZ(1,1)); XOR(PZ(3,2),PZ(1,1));
	XOR(PZ(0,2),PZ(1,2)); XOR(PZ(2,2),PZ(1,2));

	outernet_rx_lane_commit_parity_zone(lane);

	break;
      case 0xe:
	// missing piece 0

	// Copy the parity slices to the missing spot
	COPY(PB(0),PZ(0,0));
	COPY(PB(1),PZ(0,1));
	COPY(PB(2),PZ(0,2));

	// Reveal the original data
	XOR(PZ(2,1),PZ(0,0)); XOR(PZ(3,1),PZ(0,0));
	XOR(PZ(1,1),PZ(0,1)); XOR(PZ(3,2),PZ(0,1));
	XOR(PZ(1,2),PZ(0,2)); XOR(PZ(2,2),PZ(0,2));

	outernet_rx_lane_commit_parity_zone(lane);

	break;
      case 0xf:	
	// We have all four pieces, so life is easy.
	outernet_rx_lane_commit_parity_zone(lane);
	break;
    }

  } while(0);

  LOG_EXIT;
  return retVal;
}

int outernet_rx_saw_packet(unsigned char *buffer,int bytes)
{
  int retVal=0;
  LOG_ENTRY;

  do {

    unsigned int lane=buffer[0];
    unsigned int packet_mtu=buffer[3];

    if (lane>=MAX_LANES) {
      LOG_ERROR("Outernet packet is for lane #%d (we only support 0 -- %d)",
		lane,MAX_LANES-1);
      retVal=-1;
      break;
    }
    
    // 3/4 of the usable bytes are available for data
    int data_bytes=(packet_mtu-3)*3/4;
    int parity_bytes=data_bytes/3;
    if (parity_bytes*3!=data_bytes) {
      LOG_ERROR("Parity stripe size problem. MTU=%d, data_bytes=%d, parity_bytes=%d",
		packet_mtu,data_bytes,parity_bytes);
      retVal=-1;
      break;
    }

    int start_flag=0;
    int end_flag=0;
    int sequence_number=buffer[1]+(buffer[2]<<8);
    sequence_number &= 0x3fff;
    if (buffer[2]&0x40) start_flag=1;
    if (buffer[2]&0x80) end_flag=1;

    int parity_zone_size=data_bytes*4;
    int parity_zone_offset=(sequence_number*data_bytes)%parity_zone_size;
    // int parity_zone_start=(sequence_number*data_bytes)-parity_zone_offset;
    int parity_zone_number=sequence_number/4;
    int parity_zone_slice=sequence_number&3;
    
    unsigned char *data=&buffer[4];
    unsigned char *parity=&buffer[4+data_bytes];

    LOG_NOTE("Received bundle piece in lane #%d, sequence #%d (start=%d, end=%d) (parity zone #%d, packet %d)",
	     lane,
	     sequence_number,start_flag,end_flag,
	     parity_zone_number,parity_zone_slice);
    dump_bytes(stderr,"Bundle bytes",data,data_bytes);
    dump_bytes(stderr,"Parity bytes",parity,parity_bytes);

    // Start receiving if we see a start sequence, or if we see sequence #1 while waiting
    // for a start (since we can recover the missing start)
    if (start_flag||((sequence_number==1)&&outernet_rx_bundles[lane].waitingForStart)) {
      // Erase whatever was sitting in this lane.
      LOG_NOTE("Clearing lane #%d RX state for new bundle",lane);
      outernet_rx_lane_init(lane,1);

      outernet_rx_bundles[lane].waitingForStart=0;
    }

    // If we are waiting for a new start flag, ignore whatever
    // we see in the meantime.
    // If we see a sequence #1, then we might have missed
    // only sequence #0, which we can recover via parity, if we
    // don't miss the next two packets.  So we should handle that
    // special case.
    if (outernet_rx_bundles[lane].waitingForStart
	&&(sequence_number>1)) break;

    // Can only happen if we are on sequence #0 or #1, which in either
    // case counts as a start.
    outernet_rx_bundles[lane].waitingForStart=0;

    if ((parity_zone_number < outernet_rx_bundles[lane].parity_zone_number)
        && (parity_zone_number != outernet_rx_bundles[lane].last_parity_zone_number)) {
      // We seem to have gone backwards, which means that we have to
      // abandon the current transfer, as presumably we missed the end of the
      // last, and the start of this one.
      LOG_NOTE("Clearing lane #%d RX state because parity_zone_number went backwards from %d to %d",
	       lane,outernet_rx_bundles[lane].parity_zone_number,parity_zone_number);
      outernet_rx_lane_init(lane,1);
      break;
    }
    if (parity_zone_number==(1 + outernet_rx_bundles[lane].parity_zone_number))
      {
	// Parity zone has advanced by exactly one.
	// We need to check that the previous parity zone was completed.
	// If not, then we need to stop receiving.
      switch (outernet_rx_bundles[lane].parity_zone_bitmap)
	{
	case 0x0: case 0x1: case 0x2: case 0x3:
	case 0x4: case 0x5: case 0x6: case 0x8:
	case 0x9: case 0xa: case 0xc:
	  // Less than 3 pieces received from last parity zone
	  LOG_NOTE("Clearing lane #%d RX state because parity_zone_number advanced, but we havn't received at least 3/4 packets from the last one.",lane);
	  outernet_rx_lane_init(lane,1);
	  break;
	default:
	  // We have 3 or more pieces in the last parity zone,
	  // so commit it. Really this should never happen, because
	  // we should commit a parity zone after adding to it
	  // each time
	  outernet_rx_lane_update_parity_zone(lane);
	}
      }
    if (parity_zone_number==outernet_rx_bundles[lane].parity_zone_number)  {
      // Okay, the packet is for this parity zone.
      // Copy the data and parity bytes in, and update the bitmap

      // Copy the data bytes
      dump_bytes(stderr,"rxd bytes",data,data_bytes);
      dump_bytes(stderr,"stored bytes",&outernet_rx_bundles[lane].parity_zone[parity_zone_offset],data_bytes);
      memcpy(&outernet_rx_bundles[lane].parity_zone[parity_zone_offset],
	     data,data_bytes);

      /* Copy the parity bytes.
	 For now, we just keep them. It is only when we call
	 outernet_rx_lane_update_parity_zone() that we try to
	 use the parity. */
      memcpy(&outernet_rx_bundles[lane].parity_bytes[parity_zone_slice*parity_bytes],parity,parity_bytes);
      
      
      // Update bitmap
      outernet_rx_bundles[lane].parity_zone_bitmap|=(1<<parity_zone_slice);
      outernet_rx_bundles[lane].data_bytes=data_bytes;
      
      // See if we have enough received to commit the parity zone
      outernet_rx_lane_update_parity_zone(lane);
      
    }
    if (parity_zone_number > ( 1 + outernet_rx_bundles[lane].parity_zone_number)) {
      LOG_NOTE("Clearing lane #%d RX state because parity_zone_number skipped forwards",lane);
      outernet_rx_lane_init(lane,1);
      break;
    }

    if (end_flag) {
      LOG_NOTE("Attempting to insert bundle received via outernet via end_flag");
      outernet_rx_try_bundle_insert(lane);
      // Then clear the lane
      outernet_rx_lane_init(lane,1);
    
    }
    
    
    
  } while(0);
  
  LOG_EXIT;
  return retVal;
}
  

int outernet_rx_serviceloop(void)
{
  unsigned char buffer[4096];
  ssize_t bytes_recv;
  int retVal=0;
  
  LOG_ENTRY;

  do {
    bytes_recv = recvfrom( outernet_socket, buffer, sizeof(buffer), 0, 0, 0 );
    while(bytes_recv>0) {
      LOG_NOTE("Received %d bytes via Outernet UNIX domain socket",bytes_recv);

      if (outernet_rx_saw_packet(buffer,bytes_recv)) {
	retVal=-1;
	LOG_ERROR("outernet_rx_saw_packet() reported an error");
      }
      
      bytes_recv = recvfrom( outernet_socket, buffer, sizeof(buffer), 0, 0, 0 );
    }
  } while(0);

  LOG_EXIT;
  return retVal;
}

int outernet_rx_setup(char *socket_filename)
{
  int exitVal=0;

  LOG_ENTRY;
  do {
  
    // Open socket for reading from an outernet receiver.
    // (note that we can theoretically do this while also talking
    // to a radio, because the outernet one-way protocol is quite
    // separate from the usual LBARD protocol.
    LOG_NOTE("Trying to open Outernet socket");

    // Initialise data RX structures
    for(int i=0;i<MAX_LANES;i++) outernet_rx_lane_init(i,0);
    
    outernet_socket = socket( AF_UNIX, SOCK_DGRAM, 0 );
    
    if( outernet_socket < 0 ) {
      LOG_ERROR("socket() failed: (%i) %m", errno );
      exitVal=-1;
      break;
    }

    // UNIX domain datagram sockets have to be bound to their own end point
    // as well.  This is a little odd, but it is how they work.
    unlink(socket_filename);
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sun_family = AF_UNIX;
    strncpy(client_addr.sun_path, socket_filename, 104);

    if (bind(outernet_socket, (struct sockaddr *) &client_addr, sizeof(client_addr)))
      {
	perror("bind()");
	LOG_ERROR("bind()ing UNIX socket client end point failed");
	exitVal=-1;
	break;
      }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000;
    if (setsockopt(outernet_socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
      LOG_ERROR("setsockopt failed: (%i) %m", errno );
      exitVal=-1;
      break;
    }
    
    LOG_NOTE("Opened unix socket '%s' for outernet rx",socket_filename);
  } while (0);

  LOG_EXIT;
  return exitVal;  
}
