/*
  The Outernet Uplink driver is a bit different to the other drives.
  Whereas the others send one bundle at a time, and use the normal
  mechanism for doing so, we must cope with the fact that we have 
  a uni-directional communications link.  This means that we need to
  periodically assess the highest priority bundles we hold, and know
  which ones we have recently uplinked, and which we have not.

  There is some care required to tune this appropriately.
  It is important that small new bundles with high priority are transmitted
  as soon as possible, so that early warning of disaster messages can
  be received in a timely manner.
  Also, because the satellite link may end up with lost packets, we need
  to both interleave and apply some level of redundancy.  We will use 3+1
  RAID5-style parity, together with a 1:5 interleave, i.e., we will uplink
  five bundles simultaneously, with one packet from each being sent, and for
  every 3 packets, we will provide one parity packet.  This means that at
  least six consecutive packets must be lost before there will be a problem
  with reception.  However, it is of course possible that problems will still
  occur, and so we must retransmit high priority bundles repeatedly.  For now,
  this will be managed by having the Rhizome database for the uplink side having
  to exercise restraint at the number of bundles that it is pushing.  To also
  help minimise latency, the five simultaneous upload lanes will be allocated
  to different bundle sizes, similar to how Rhizome over Wi-Fi works, so that
  a large bundle can continue to be uplinked without preventing the immediate
  uplink of new small bundles.  

  Thus, for each of the five uplink lanes, we should produce a list of the bundles
  in it, and uplink them endlessly in a loop.  When a new bundle is detected (or
  a new version of an old bundle), it should most likely be immediately uplinked.

  The remaining question is whether we should use the priority score of the bundles
  within a lane to affect the frequency of uplink of each, i.e., so that high
  priority bundles can be uplinked repeatedly over a relatively short period of time.
*/
/*
The following specially formatted comments tell the LBARD build environment about this radio.
See radio_type for the meaning of each field.
See radios.h target in Makefile to see how this comment is used to register support for the radio.

RADIO TYPE: OUTERNET,"outernet","Outernet.is broadcast satellite",outernet_radio_detect,outernet_serviceloop,outernet_receive_bytes,outernet_send_packet,outernet_check_if_ready,10

*/

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <time.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "sync.h"
#include "lbard.h"
#include "hf.h"
#include "radios.h"
#include "code_instrumentation.h"

long long last_uplink_packet_time=0;
int last_uplink_lane=-1;

#define MAX_MTU 255
unsigned char outernet_packet[MAX_MTU];
int outernet_packet_len=0;
int outernet_mtu=200;
int outernet_sequence_number=0;

// Import serial_port string from main.c
extern char *serial_port;

// Address of IP link
struct sockaddr_in addr_uplink;
int uplink_fd=-1;

// TX queues for each lane.
struct outernet_lane_tx_queue {
  int bundle_numbers[MAX_BUNDLES];
  int queue_len;

  // Size of bundles this lane handles
  int min_size;
  int max_size;

  // Flattened form of bundle we are currently uplinking.
  int serialised_bundle_number;
  unsigned char *serialised_bundle;
  int serialised_len;
  int serialised_offset;
};

#define UPLINK_LANES 5
struct outernet_lane_tx_queue *lane_queues[UPLINK_LANES]={NULL};

int outernet_lane_queue_setup(void)
{
  int retVal=0;
  LOG_ENTRY;
  
  do {
    // Allocate queues
    for(int i=0;i<UPLINK_LANES;i++) {
      if (!lane_queues[i]) {
	lane_queues[i]=calloc(sizeof(struct outernet_lane_tx_queue),1);
	if (!lane_queues[i]) {
	  LOG_ERROR("calloc() of lane_queue[%d] failed. Out of memory?",i);
	  retVal=-1;
	}
	else {
	  /* Set size thresholds for each lane.
	     We will use the following cut points:
	     Lane 0 - < 1KiB
	     Lane 1 - < 4KiB
	     Lane 2 - < 16KiB
	     Lane 3 - < 64KiB
	     Lane 4 - All others.
	     (i.e., 1KiB << (lane * 2))
	  */
	  if (i) lane_queues[i]->min_size=1+ (1<<(10+i+i));
	  else lane_queues[i]->min_size=0;
	  if (i<4) 
	    lane_queues[i]->max_size=(1<<(10+(i+1)+(i+1)))-1;
	  else
	    lane_queues[i]->max_size=0x7fffffff;	    
	  // Not currently uplinking anything
	  lane_queues[i]->serialised_bundle_number=-1;

	  LOG_NOTE("Outernet uplink lane #%d successfully initialised.",i);
	}
      }
    }

  } while(0);
  LOG_EXIT;
  return retVal;
}

int outernet_uplink_next_in_queue(int lane)
{
  /* Pick the first item in the queue, serialise it, and mark
     it ready for uplinking.

     We then put that item to the end of the queue, so that we 
     round-robin through all the items in the lane's queue.

     XXX -- We should have a prioritisation scheme, once we have
     a means of setting the priority of the bundles. For now this
     has to be managed by not having too many bundles in the
     queue for the lane.
  */

  int retVal=0;
  LOG_ENTRY;
  
  do {
    int bundle=-1;
    int source_lane=lane;
    
    // Work out next bundle
    if (lane<0||lane>4) { retVal=-1; break; } // lane exists?
    if (!lane_queues[lane]->queue_len) {
      // There is nothing in the queue for this lane.
      // We are allowed to take things from the queue of lower
      // numbered lanes.
      for(source_lane=0;source_lane<lane;source_lane++)
	if (lane_queues[source_lane]->queue_len) break;
      LOG_NOTE("source_lane = %d (for lane #%d)",source_lane,lane);
      // If still nothing to do, then exit
      if (!lane_queues[source_lane]->queue_len) break;      
    }

    if (lane_queues[lane]->serialised_bundle_number!=-1) {
      LOG_ERROR("Must dequeue bundle being transmitted before calling outernet_upline_next_in_queue() for lane #%d",lane);
      retVal=-1;
      break;
    }
    
    // Get bundle # of head of queue
    bundle=lane_queues[source_lane]->bundle_numbers[0];
    // Move head of queue to tail of queue
    int n;
    for(n=0;n<lane_queues[source_lane]->queue_len-1;n++)
      lane_queues[source_lane]->bundle_numbers[n]
	=lane_queues[source_lane]->bundle_numbers[n+1];
    lane_queues[source_lane]->bundle_numbers[n]=bundle;
    
    // Get requested bundle in the bundle cache
    if (prime_bundle_cache(bundle,
			   my_sid_hex,servald_server,credential))
      {
	LOG_ERROR("Failed to prime bundle cache for bundle #%d",bundle);
      }
    if (cached_body_len<0||cached_manifest_encoded_len<0) {
      LOG_NOTE("Cached length is negative");
      retVal=-1;
      break;
    }
    
    /* Build serialised version.
       We use a very simple file format:
       2 bytes = length of encoded manifest,
       followed by manifest and body.
    */       
    int serialised_len=2+4+cached_manifest_encoded_len+cached_body_len;
    // (but allow extra space for a complete parity zone, so that packet building is simpler)
    unsigned char *serialised_data=malloc(serialised_len+MAX_MTU*4);
    if (!serialised_data) {
      LOG_ERROR("Could not allocate buffer for serialised data for bundle #%d (manifest len=%d, body len=%d)",
		bundle,cached_manifest_encoded_len,cached_body_len);
      retVal=-1;
      break;
    }

    // Set the fields in the serialised data
    serialised_data[0]=(cached_manifest_encoded_len>>0)&0xff;
    serialised_data[1]=(cached_manifest_encoded_len>>8)&0xff;
    serialised_data[2]=(cached_body_len>>0)&0xff;
    serialised_data[3]=(cached_body_len>>8)&0xff;
    serialised_data[4]=(cached_body_len>>16)&0xff;
    serialised_data[5]=(cached_body_len>>24)&0xff;
    bcopy(cached_manifest_encoded,&serialised_data[2+4],cached_manifest_encoded_len);
    bcopy(cached_body,&serialised_data[2+4+cached_manifest_encoded_len],cached_body_len);
    // Put a safe empty region at the end, so the last parity block can be correctly
    // calculated, regardless of the length of the serialised bundle modulo parity block
    // size.
    bzero(&serialised_data[2+4+cached_manifest_encoded_len+cached_body_len],MAX_MTU*4);

    // Store in lane
    lane_queues[lane]->serialised_bundle=serialised_data;
    lane_queues[lane]->serialised_offset=0;
    lane_queues[lane]->serialised_bundle_number=bundle;
    lane_queues[lane]->serialised_len=serialised_len;

    LOG_NOTE("Bundle #%d serialised and ready for uplink in lane #%d (pulled from queue of lane #%d)",
	     bundle,lane,source_lane);
    
  } while(0);
  LOG_EXIT;
  return retVal;
}


int outernet_uplink_lane_dequeue_current(int lane)
{
  LOG_ENTRY;

  lane_queues[lane]->serialised_bundle_number=-1;
  if (lane_queues[lane]->serialised_bundle)
    free(lane_queues[lane]->serialised_bundle);
  lane_queues[lane]->serialised_bundle=NULL;
  
  outernet_uplink_next_in_queue(lane);
  
  return 0;
  LOG_EXIT;
}

int outernet_uplink_lane_dequeue_bundle(int lane,int bundle)
{
  LOG_ENTRY;
  // Remove from active uplink if it was being uplinked.
  if (lane_queues[lane]->serialised_bundle_number==bundle) {
    LOG_NOTE("Stopping uplink of bundle #%d in lane #%d",
	     bundle,lane);
    outernet_uplink_lane_dequeue_current(lane);
  }

  // Remove the bundle from the queue, if present.
  for(int n=0;n<lane_queues[lane]->queue_len;n++)
    {
      if (lane_queues[lane]->bundle_numbers[n]==bundle) {
	LOG_NOTE("Removing bundle #%d from outernet uplink lane #%d",
		 bundle,lane);
	for(int m=n;m<(lane_queues[lane]->queue_len-1);m++)
	  lane_queues[lane]->bundle_numbers[m]=
	    lane_queues[lane]->bundle_numbers[m+1];
	lane_queues[lane]->queue_len--;
	break;
      }
    }

  return 0;
  LOG_EXIT;
}

int outernet_upline_queue_triage(void)
{
  /* Look at new/updated bundles, and update the uplink lane queues.
     NOTE: As the uplink server will typically run on well resourced hardware,
     we don't have to be quite so careful about run time and memory use here.
  */

  int retVal=0;
  LOG_ENTRY;

  // Go through newly arrived/updated bundles
  if (fresh_bundle_count) 
    LOG_NOTE("Examining %d fresh bundles.",fresh_bundle_count);
  for(int i=0;i<fresh_bundle_count;i++) {
    int b=fresh_bundles[i];
    int lane;
    LOG_NOTE("Triaging fresh bundle: bundle #%d",b);
    LOG_NOTE("bundles[b].length=%lld",bundles[b].length);
    for(lane=0;lane<5;lane++) {
      if (lane_queues[lane]) {
	if ((bundles[b].length>=lane_queues[lane]->min_size)
	    &&(bundles[b].length<=lane_queues[lane]->max_size)) {
	  LOG_NOTE("Newly received bundle #%d of length %d goes in lane #%d\n",
		   b,bundles[b].length,lane);
	  if (lane_queues[lane]->queue_len>=MAX_BUNDLES) {
	    LOG_ERROR("Uplink lane #%d is full. This should not be possible.",lane);
	    break;
	  }
	  int bb;
	  // Check if already queued
	  for(bb=0;bb<lane_queues[lane]->queue_len;bb++)
	    if (b==lane_queues[lane]->bundle_numbers[bb]) {
	      LOG_NOTE("Bundle #%d remains in lane #%d after update.",
		       b,lane);
	      break;
	    }
	  if (bb==lane_queues[lane]->queue_len) {
	    LOG_NOTE("Bundle #%d added to uplink lane #%d.",
		     b,lane);
	    lane_queues[lane]->bundle_numbers[lane_queues[lane]->queue_len++]=b;
	  } else {
	    // This bundle wasn't previously in this lane, so check
	    // the other lanes, in case it has changed size and needs
	    // to move from one lane to another
	    for(int l=0;l<5;l++)
	      if (l!=lane) outernet_uplink_lane_dequeue_bundle(l,b);
	  }
	  break;
	}
      }
    }
    if (lane==5) {
      LOG_WARN("Newly received bundle #%d of length %d doesn't match any lane, so will not be uplinked\n",
	       b,bundles[b].length);
      retVal++;
      if (retVal<1) retVal=1;
    }
  }

  fresh_bundle_count=0;

  LOG_EXIT;
  return retVal;
}

int outernet_radio_detect(int fd)
{
  /*
    The outernet.is satellite service has a UDP packet injection interface,
    and the receivers also provide received packets from the satellite via
    UDP packets, so we can use a common configuration scheme for both.
    Well, actually, we want LBARD to accept packets from a nearby Outernet 
    receiver, even if it is using a different radio driver, so we really
    only want a driver for the uplink side of the Outernet service.

    Basically we want to open a UDP socket to the outernet server, which we
    will parse from the serial_port string.    
  */

  LOG_NOTE("Beginning Outernet auto detection");
  
  int retVal=-1;
  
  char hostname[1024]="";
  int port=-1;

  struct in_addr hostaddr={0};

  LOG_ENTRY;
  
  do {
    int fields=sscanf(serial_port,"outernet://%[^:]:%d",hostname,&port);
    LOG_NOTE("Parsed %d fields",fields);
    if (fields==2) {
      fprintf(stderr,"Parsed Outernet URI. Host='%s', port='%d'\n",hostname,port);
      LOG_NOTE("Parsed Outernet URI. Host='%s', port='%d'\n",hostname,port);
      
      if (inet_aton(hostname,&hostaddr)==1) {
	LOG_NOTE("Parsed hostname as IPv4 address");
      } else {

	LOG_NOTE("Attempting to resolve hostname '%s' to IP address",hostname);
	struct hostent *he=gethostbyname(hostname);
	
	if (!he) {
	  LOG_ERROR("Failed to resolve hostname '%s' to IP",hostname);
	  break;
	}
	struct in_addr **addr_list=(struct in_addr **) he->h_addr_list;
	
	if (!addr_list) {
	  fprintf(stderr,"Could not get IP for hostname '%s' (h_addr_list empty)",hostname);
	  LOG_ERROR("Could not get IP for hostname '%s' (h_addr_list empty)",hostname);
	  break;
	}

	// XXX - We assume IPv4 addressing here! We should support IPv6 as well
	if (he->h_addrtype!=AF_INET) {
	  LOG_ERROR("Address of '%s' is not IPv4",hostname);
	  break;
	}
	
	hostaddr=*addr_list[0];
      }

      fprintf(stderr,"Host address of '%s' is %08x\n",hostname,hostaddr.s_addr);
      
      struct sockaddr_in addr_us;
      
      bzero((char *) &addr_us, sizeof(struct sockaddr_in));
      bzero((char *) &addr_uplink, sizeof(struct sockaddr_in));
      
      // Set up address for our side of the socket
      addr_us.sin_family = AF_INET;
      addr_us.sin_port = htons(port);
      addr_us.sin_addr.s_addr = htonl(INADDR_ANY);
      
      // Setup address for Outernet's server
      addr_uplink.sin_family = AF_INET;
      addr_uplink.sin_port = htons(port);
      addr_uplink.sin_addr.s_addr = hostaddr.s_addr;
      
      if ((uplink_fd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
	  perror("Failed to create UDP socket");
	  LOG_ERROR("Failed to create UDP socket");
	  break;
	}    

      //      if( bind(fd, (struct sockaddr*)&addr_us, sizeof(struct sockaddr_in) ) == -1)
      //	{
      //	  perror("Failed to bind UDP socket");
      //	  LOG_ERROR("Failed to bind UDP socket");
      //	  break;
      //	}
      
      // XXX to the other missing steps
      
      // Successfully connected
      LOG_NOTE("Detected radio as Outernet");
      radio_set_type(RADIOTYPE_OUTERNET);
      outernet_lane_queue_setup();
      retVal=1; // successfully autodetected, stop auto-detect process
    } else {
      LOG_NOTE("URI is not for outernet uplink: '%s'",serial_port);
    }
  }
  while(0);
  
  LOG_EXIT;
  return retVal;
}

int outernet_check_if_ready(void)
{
  return -1;
}

/*
  For simplicity, we don't want to have separate parity packets.
  Rather, we want a constant amount of data in each packet, and 
  a constant amount of parity.  That way, we can use a simple 
  offset pointer, and it will get us the right piece of data, and
  calculate the right piece of parity. This relies on the MTU not
  changing during the uplink of a bundle, as it will mess up all
  the calculations.
  
  The packets need to include the sequence number and lane number,
  and also a unique identifier for the bundle. Ideally that
  identifier should be the same for if the same bundle is sent
  again, so that we can recover from high levels of packet loss
  through repetition of the whole bundle.  The trade-off is that
  we then need to spend more bytes on the identifier, which we 
  have to be careful of, because our MTU is likely to be only
  around 200 bytes.  The more bandwidth efficient approach, but
  at the expense of being able to use re-transmissions of the whole
  bundle, is to simply mark packets as start and/or end packets,
  similar to how we do in normal LBARD radio packets.  These two
  bits can be merged in with the sequence number.

  So, we then need in each packet:

  1 byte = data unit size, i.e., the fixed number of data bytes
  per packet. Used to calculate parity regions.
  2 bytes = sequence number (16K values = ~6 hour turn over), plus
  start/end of bundle markers.
  n bytes = data.
  n/3 bytes = parity stripe.

*/
 
int outernet_uplink_build_packet(int lane)
{
  int retVal=0;
  LOG_ENTRY;

  do {
    if (lane<0||lane>4) { retVal=-1; break;}
    if (!lane_queues[lane]) { retVal=-1; break;}
    if (lane_queues[lane]->serialised_bundle_number==-1)
       { retVal=-1; break;}
  
    // 3/4 of the usable bytes are available for data
    int data_bytes=(outernet_mtu-(1+2+1))*3/4;
    int parity_bytes=data_bytes/3;
    if (parity_bytes*3!=data_bytes) {
      LOG_ERROR("Parity stripe size problem. MTU=%d, data_bytes=%d, parity_bytes=%d",
		outernet_mtu,data_bytes,parity_bytes);
      retVal=-1;
      break;
    }

    // Work out where parity zone lies, i.e., which 3 packets
    // worth of data.
    int parity_zone_size=data_bytes*4;
    int parity_zone_start=lane_queues[lane]->serialised_offset
      -(lane_queues[lane]->serialised_offset%parity_zone_size);
    // Within that, work out where the parity stripe lies, i.e.,
    // which third of a packet offset.
    int parity_stripe_number=
      (lane_queues[lane]->serialised_offset-parity_zone_start)
      /data_bytes;
    LOG_NOTE("serialised_offset=%d, parity_zone_start=%d, parity_stripe_number=%d, data_bytes=%d, parity_bytes=%d, serialised_len=%d",
	     lane_queues[lane]->serialised_offset,
	     parity_zone_start,parity_stripe_number,
	     data_bytes,parity_bytes,
	     lane_queues[lane]->serialised_len	     );

    /* Generate the parity stripe for this zone.
       For each block of data, we XOR together 1/3 of each of the other 
       three data blocks in the parity zone to produce the parity stripe.
       
    */
    unsigned char *dataStart=&lane_queues[lane]->serialised_bundle[parity_zone_start];
    unsigned char parity_stripe[MAX_MTU];
    unsigned char *srcA,*srcB,*srcC;
    switch (parity_stripe_number) {
    case 0:
      srcA=&dataStart[data_bytes*1+parity_bytes*0];
      srcB=&dataStart[data_bytes*2+parity_bytes*0];
      srcC=&dataStart[data_bytes*3+parity_bytes*0];
      break;
    case 1:
      srcA=&dataStart[data_bytes*0+parity_bytes*0];
      srcB=&dataStart[data_bytes*2+parity_bytes*1];
      srcC=&dataStart[data_bytes*3+parity_bytes*1];
      break;
    case 2:
      srcA=&dataStart[data_bytes*0+parity_bytes*1];
      srcB=&dataStart[data_bytes*1+parity_bytes*1];
      srcC=&dataStart[data_bytes*3+parity_bytes*2];
      break;
    case 3:
      srcA=&dataStart[data_bytes*0+parity_bytes*2];
      srcB=&dataStart[data_bytes*1+parity_bytes*2];
      srcC=&dataStart[data_bytes*2+parity_bytes*2];
      break;
    }
    for(int i=0;i<parity_bytes;i++) parity_stripe[i]=srcA[i]^srcB[i]^srcC[i];

    // Glue everything together:

    bcopy(&lane_queues[lane]->serialised_bundle[lane_queues[lane]->serialised_offset],&outernet_packet[1+2+1],data_bytes);
    bcopy(parity_stripe,&outernet_packet[1+2+1+data_bytes],parity_bytes);
    
    // Sequence number
    int seq=lane_queues[lane]->serialised_offset/data_bytes;
    // Start of bundle marker
    if (!lane_queues[lane]->serialised_offset) seq|=0x4000;

    lane_queues[lane]->serialised_offset+=data_bytes;

    // Send to end of serialised bundle + rounded out to end of parity zone, to make sure end of
    // bundle is protected as well as the rest of it is.
    if ((lane_queues[lane]->serialised_offset>=lane_queues[lane]->serialised_len)
        &&(parity_stripe_number==3)) {
      // Last packet in bundle
      seq|=0x8000;
      // So get ready for next one
      outernet_uplink_lane_dequeue_current(lane);
    }

    outernet_packet[0]=lane&0xff;
    outernet_packet[1]=(seq>>0)&0xff;
    outernet_packet[2]=(seq>>8)&0xff;
    outernet_packet[3]=outernet_mtu;

    outernet_packet_len=1+2+1+data_bytes+parity_bytes;
    dump_bytes(stderr,"Packet for uplink",outernet_packet,outernet_packet_len);
    
  } while(0);
  
  LOG_EXIT;
  return retVal;
}
				 

int outernet_serviceloop(int serialfd)
{
  int retVal=0;
  LOG_ENTRY;

  do {
    // Check if the uplink queues need updating due to the arrival
    // of new or updated bundles.
    // XXX - We don't currently have a means of deleting bundles
    // from lane queues that we no longer have.
    outernet_upline_queue_triage();
    
    if ((gettime_ms()-last_uplink_packet_time)<0) {
      // Time went backwards.
      last_uplink_packet_time=gettime_ms();
    }

    // Check for ACK packet from Outernet uplink
    {
      unsigned char rx_buf[4096];
      int rx_bytes=4096;
      set_nonblock(uplink_fd);
      rx_bytes = recvfrom(uplink_fd, rx_buf, rx_bytes, 0, NULL, 0);
      if (rx_bytes>0) {
	LOG_NOTE("Received %d bytes from uplink",rx_bytes);
	dump_bytes(stdout,"ack packet",rx_buf,rx_bytes);
      }
      //      if  ((rx_bytes==outernet_packet_len)
      //	   &&(!memcmp(rx_buf,outernet_packet,rx_bytes)))
      if (rx_bytes>0)
	{
	  LOG_NOTE("My packet was acknowledge. I can now send the next.");
	  last_uplink_packet_time=0;
	}
    }
    
    if ((gettime_ms()-last_uplink_packet_time)>60000) {
      // 1 minute has passed, or we have received an ack for our last
      // packet, so send next uplink packet.
      // Note that if some lanes don't have anything to send,
      // we don't just send the next thing for another lane,
      // because the interlacing of lanes is to separate successive
      // packets in time for a single lane sufficiently to provide
      // some tolerance against fade.
      // Thus if we have nothing to send for the current lane, we
      // send whatever it was we sent last time.  Thus we gain some
      // further redundancy.  This could of course be greatly
      // improved, for example by remembering the last n packets
      // for each lane, and sending older ones, so that we have a
      // longer effective window.  But for now, the goal is to prove
      // the concept, and get something that works reasonably.
      
      last_uplink_lane++;
      if (last_uplink_lane<0||last_uplink_lane>4) last_uplink_lane=0;
      if (lane_queues[last_uplink_lane]) {
	// Make sure we have something to uplink
	if (lane_queues[last_uplink_lane]->serialised_bundle_number==-1) {
	  LOG_NOTE("Nothing queued in lane #%d, so trying to queue something",last_uplink_lane);
	  outernet_uplink_next_in_queue(last_uplink_lane);
	}
	// If still nothing to uplink, then do nothing, i.e.,
	// resend last packet.  Else, replace packet content.
	if (lane_queues[last_uplink_lane]->serialised_bundle_number==-1) {
	  // This queue is idle, try to get something on it
	  outernet_uplink_next_in_queue(last_uplink_lane);
	}
	if (lane_queues[last_uplink_lane]->serialised_bundle_number!=-1) {
	  LOG_NOTE("Something in the queue for lane #%d",last_uplink_lane);
	  outernet_uplink_build_packet(last_uplink_lane);
	}
      }

      if (outernet_packet_len>0) {
	last_uplink_packet_time=gettime_ms();
	
	// Uplink the packet
	unsigned int sent_bytes;
	sent_bytes = sendto(uplink_fd, outernet_packet, outernet_packet_len, 0, (struct sockaddr *)&addr_uplink, sizeof(struct sockaddr));
	if ( sent_bytes != outernet_packet_len ) {
	  LOG_ERROR("[udp] sendto failed: pkt size %d, sent %d. (%i) %m ", outernet_packet_len, sent_bytes, errno );
	  retVal=-1; break;
	} else LOG_NOTE("Packet of %d bytes uplinked.",outernet_packet_len);     
      } else {
	// We had nothing to uplink just yet, so try again in 1 second (ie, set time out to 1 second
	// before the 60 second timeout.
	last_uplink_packet_time=gettime_ms()-60000+1000;	
      }
    }
  } while(0);
  
  LOG_EXIT;
  return retVal;
}

int outernet_receive_bytes(unsigned char *bytes,int count)
{ 
  // This driver uses a UDP outbound only arrangement, so nothing to do here.
  return 0;
}

int outernet_send_packet(int serialfd,unsigned char *out, int len)
{
  // Packets in this driver are sent from the service loop, because it doesn't
  // use the normal LBARD bi-directional protocol.
  return 0;
}

