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

extern long long status_dump_epoch;

long long next_time_update_allowed_after=0;

int saw_timestamp(char *sender_prefix,int stratum, struct timeval *tv)
{
  if (debug_radio)
    printf("Saw timestamp from %s: stratum=0x%02x, our stratum=0x%02x.0x%02x\n",
	   sender_prefix,stratum,my_time_stratum>>8,my_time_stratum&0xff);

  if (tv->tv_usec>999999) { tv->tv_sec++; tv->tv_usec-=1000000; }

  if (gettime_ms()>next_time_update_allowed_after) {
    if ((stratum<(my_time_stratum>>8))) {
      // Found a lower-stratum time than our own, and we have enabled time
      // slave mode, so set system time.
      // Then update our internal timers accordingly
      if (time_slave&&(!monitor_mode)) {
	struct timeval before,after;

	account_time_pause();

	gettimeofday(&before,NULL);
	settimeofday(tv,NULL);
	gettimeofday(&after,NULL);
	long long delta=
	  after.tv_sec*1000+(after.tv_usec/1000)
	  -
	  before.tv_sec*1000+(before.tv_usec/1000);
	if (next_message_update_time) next_message_update_time+=delta;
	if (congestion_update_time) congestion_update_time+=delta;
	if (last_status_time) last_status_time+=delta;
	if (radio_last_heartbeat_time) radio_last_heartbeat_time+=delta;
	log_rssi_timewarp(delta);
	if (status_dump_epoch) status_dump_epoch+=delta;
	if (last_servald_contact) last_servald_contact+=delta;
	
	account_time_resume();
	
	if (delta<-2000) {
	  // Time went backwards: This can cause trouble for servald alarms.
	  // Best solution: Tell servald to stop and let runservald restart it.
	  system(SERVALD_STOP);
	}
	
	// Don't touch time again for a little while
	// (Updating time possibly several times per second might upset things)
	next_time_update_allowed_after=gettime_ms()+20000;
      }
      // By adding only one milli-strata, we effectively match the stratum that
      // updated us for the next 256 UHF packet transmissions. This should give
      // us the stability we desire.
      if (debug_radio)
	printf("Saw timestamp from %s with stratum 0x%02x,"
	       " which is better than our stratum of 0x%02x.0x%02x\n",
	       sender_prefix,stratum,my_time_stratum>>8,my_time_stratum&0xff);
      my_time_stratum=((stratum+1)<<8);
    }
  }
  if (monitor_mode)
    {
      char monitor_log_buf[1024];
      time_t current_time=tv->tv_sec;
      struct tm tm;
      localtime_r(&current_time,&tm);
      
      snprintf(monitor_log_buf,sizeof(monitor_log_buf),
	       "Timestamp: stratum=0x%02X, "
	       "time %04d/%02d/%02d %02d:%02d.%02d (%14lld.%06lld)",
	       stratum,
	       tm.tm_year+1900,tm.tm_mon,tm.tm_mday+1,
	       tm.tm_hour,tm.tm_min,tm.tm_sec,
	       (long long)tv->tv_sec,(long long)tv->tv_usec);
      
      monitor_log(sender_prefix,NULL,monitor_log_buf);
    }
  return 0;
}

int append_timestamp(unsigned char *msg_out,int *offset)
{
  // T + (our stratum) + (64 bit seconds since 1970) +
  // + (24 bit microseconds)
  // = 1+1+8+3 = 13 bytes
  struct timeval tv;
  gettimeofday(&tv,NULL);    
  
  msg_out[(*offset)++]='T';
  msg_out[(*offset)++]=my_time_stratum>>8;
  for(int i=0;i<8;i++)
    msg_out[(*offset)++]=(tv.tv_sec>>(i*8))&0xff;
  for(int i=0;i<3;i++)
    msg_out[(*offset)++]=(tv.tv_usec>>(i*8))&0xff;
  return 0;
}


int message_parser_54(struct peer_state *sender,char *sender_prefix,
		      char *servald_server, char *credential,
		      unsigned char *msg,int length)
{
  int offset=0;
  {
    offset++;
    int stratum=msg[offset++];
    struct timeval tv;
    bzero(&tv,sizeof (struct timeval));
    for(int i=0;i<8;i++) tv.tv_sec|=msg[offset++]<<(i*8);
    for(int i=0;i<3;i++) tv.tv_usec|=msg[offset++]<<(i*8);
    /* XXX - We don't do any clever NTP-style time correction here.
       The result will be only approximate, probably accurate to only
       ~10ms - 100ms per stratum, and always running earlier and earlier
       with each stratum, as we fail to correct the received time for 
       transmission duration.
       We can at least try to fix this a little:
       1. UHF radio serial speed = 230400bps = 23040cps.
       2. Packets are typically ~250 bytes long.
       3. Serial TX speed to radio is thus ~10.8ms
       4. UHF Radio air speed is 128000bps.
       5. Radio TX time is thus 250*8/128000= ~15.6ms
       6. Total minimum delay is thus ~26.4ms
       
       Thus we will make this simple correction of adding 26.4ms.
       
       The next challenge is if we have multiple sources with the same stratum
       giving us the time.  In that case, we need a way to choose a winner, since
       we are not implementing fancy NTP-style time integration algorithms. The
       trick is to get something simple, that stops clocks jumping backwards and
       forwards allover the shop.  A really simple approach is to have a timeout
       when updating the time, and ignore updates from the same time stratum for
       the next several minutes.  We should also decay our stratum if we have not
       heard from an up-stream clock lately, so that we always converge on the
       freshest clock.  In fact, we can use the slow decay to implement this
       quasi-stability that we seek.
    */	
    tv.tv_usec+=26400;
    
    char sender_prefix[128];	
    sprintf(sender_prefix,"%s*",sender->sid_prefix);
    
    saw_timestamp(sender_prefix,stratum,&tv);
    
    // Also record time delta between us and this peer in the relevant peer structure.
    // The purpose is to that the bundle/activity log can be more easily reconciled with that
    // of other mesh extenders.  By being able to relate the claimed time of each mesh extender
    // against each other, we can hopefully quite accurately piece together the timing of bundle
    // transfers via UHF, for example.
    time_t now =time(0);
    long long delta=(long long)now-(long long)sender->last_timestamp_received;
    // fprintf(stderr,"Logging timestamp message from %s (delta=%lld).\n",sender_prefix,delta);
    if (delta<0) {
      fprintf(stderr,"Correcting last timestamp report time to be in the past, not future.\n");
      sender->last_timestamp_received=0;
    }
    if (delta>60) {
      // fprintf(stderr,"Logging timestamp message, since >60 seconds since last seen from this peer.\n");	  
      sender->last_timestamp_received=now;
      FILE *bundlelogfile=NULL;
      if (debug_bundlelog) {
	bundlelogfile=fopen(bundlelog_filename,"a");
	if (bundlelogfile) {
	  fprintf(bundlelogfile,"%lld:T+%lldms:PEERTIME:%s:%lld:%s",
		  (long long)now,
		  (long long)(gettime_ms()-start_time),sender_prefix,
		  (long long)(tv.tv_sec-now),ctime(&now));
	  fprintf(stderr,"Logged timestamp message.\n");
	  fclose(bundlelogfile);
	} else perror("Could not open bundle log file");
      } else fprintf(stderr,"Logging timestamps disabled via debug_bundlelog.\n");
    } else fprintf(stderr,"Not logging timestamp message, since we logged one just recently (%lld-%lld = %lld).\n",
		   (long long)now,(long long)sender->last_timestamp_received,
		   (long long)now-(long long)sender->last_timestamp_received);		       
    
  }

  return offset;
}

