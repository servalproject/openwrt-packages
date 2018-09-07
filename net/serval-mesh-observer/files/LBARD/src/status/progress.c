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

char message_buffer[16384];
int message_buffer_size=16384;
int message_buffer_length=0;


int generate_segment_progress_string(int stream_length,
				     struct segment_list *s, char *progress)
{
  // Apply some sanity when dealing with manifests where we don't know the length yet.
  if (stream_length<1) stream_length=1024;

  int bin;
  
  for(bin=0;bin<10;bin++) progress[bin]=' ';
  

  
  while(s) {
    int bin;

    for(bin=0;bin<10;bin++) {
      int start_of_bin=stream_length*bin/10;
      int end_of_bin=stream_length*(bin+1)/10-1;
      if ((s->start_offset<=start_of_bin)
	  &&((s->start_offset+s->length-1)>=end_of_bin))
	{
	  progress[bin]='#';
	}
      else if ((s->start_offset>=start_of_bin)
	       &&((s->start_offset+s->length-1)<end_of_bin)) {
	switch(progress[bin]) {
	case ' ': progress[bin]='.'; break;
	case '.': progress[bin]=':'; break;
	case ':': progress[bin]='+'; break;
	case '#': break;
	default: progress[bin]='?'; break;
	}
      }
    }
    s=s->next;
  }
  return 0;
}

int generate_progress_string(struct partial_bundle *partial,
			     char *progress,int progress_size)
{
  if (progress_size<1) return -1;
  progress[0]=0;
  if (progress_size<80) return -1;

  // Draw up template
  snprintf(progress,80,"M          /B           ");
  
  generate_segment_progress_string(partial->manifest_length,partial->manifest_segments,
				   &progress[1]);
  generate_segment_progress_string(partial->body_length,partial->body_segments,
				   &progress[13]);


  int manifest_bytes =0,body_bytes=0;
  struct segment_list *s=partial->manifest_segments;
  while(s) {
    manifest_bytes+=s->length;
    s=s->next;
  }
  s=partial->body_segments;
  while(s) {
    body_bytes+=s->length;
    s=s->next;
  }

  if (partial->recent_bytes)
    snprintf(&progress[24],54," %d/%d, %d/%d  [%d since last report]",
	     manifest_bytes,partial->manifest_length,
	     body_bytes,partial->body_length,
	     partial->recent_bytes
	     );
  else
    snprintf(&progress[24],54," %d/%d, %d/%d",
	     manifest_bytes,partial->manifest_length,
	     body_bytes,partial->body_length
	     );
  
  partial->recent_bytes=0;
  
  return 0;
}

int show_progress(FILE *f, int verbose)
{
  int i;

  fprintf(f,"%s",message_buffer);
  message_buffer_length=0; message_buffer[0]=0;

  int count=0;
  int progress_has_occurred=verbose;

  for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
    if (progress_has_occurred) break;
    if (partials[i].bid_prefix)
      if (partials[i].recent_bytes)
	progress_has_occurred=1;
  }
      
  if (progress_has_occurred) {
    for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
      if (partials[i].bid_prefix) {
	// Here is a bundle in flight
	char *bid_prefix=partials[i].bid_prefix;
	long long version=partials[i].bundle_version;
	char progress_string[80];
	if (!count) {
	  fprintf(f,
		  ">> List of bundles currently being received"
		  " (%d in rhizome store)\n",
		  bundle_count);
	}
	count++;
	generate_progress_string(&partials[i],
				 progress_string,sizeof(progress_string));
	fprintf(f,"   %s* version %-18lld: [%s]\n",
		bid_prefix,version,progress_string);
	}
    }
    if (count) fprintf(f,"<< end of bundle transfer list.\n");
  }
  progress_report_bundle_receipts(f);
  
  return 0;
}

int show_progress_json(FILE *f, int verbose)
{
  int i;

  int count=0;
  int progress_has_occurred=verbose;

  for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
    if (progress_has_occurred) break;
    if (partials[i].bid_prefix)
      if (partials[i].recent_bytes)
	progress_has_occurred=1;
  }
      
  if (progress_has_occurred) {
    for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
      if (partials[i].bid_prefix) {
	// Here is a bundle in flight
	char *bid_prefix=partials[i].bid_prefix;
	long long version=partials[i].bundle_version;
	char progress_string[80];
	generate_progress_string(&partials[i],
				 progress_string,sizeof(progress_string));
	fprintf(f,"%s{ \"id\": \"%s\", \"version\": %lld,"
		" \"progress\": \"%s\" }",
		count?",\n":"\n",
		bid_prefix,version,progress_string);
	count++;
	}
    }
    if (count) fprintf(f,"\n");
  }
  // progress_report_bundle_receipts(f);
  
  return 0;
}


#define MAX_RECEIVED_BUNDLES 10

struct received_bundle {
  long long version;
  char *bid_prefix_hex;
  long long rx_time;
  int reportedP;
};

struct received_bundle received_bundles[MAX_RECEIVED_BUNDLES];
int received_bundle_count=0;

int progress_log_bundle_receipt(char *bid_prefix, long long version)
{
  // Shuffle list down and make space
  
  if (received_bundle_count<MAX_RECEIVED_BUNDLES)
    received_bundle_count++;
  else free(received_bundles[MAX_RECEIVED_BUNDLES-1].bid_prefix_hex);
  
  for(int i=MAX_RECEIVED_BUNDLES-1;i>0;i--)
    received_bundles[i]=received_bundles[i-1];

  // Record newly received bundle
  received_bundles[0].rx_time=gettime_ms();
  received_bundles[0].version=version;
  received_bundles[0].bid_prefix_hex=strdup(bid_prefix);
  received_bundles[0].reportedP=0;

  return 0;
}

int progress_report_bundle_receipts(FILE *f)
{
  int newstuff=0;
  
  for(int i=0;i<received_bundle_count;i++)
    if (!received_bundles[0].reportedP) newstuff=1;

  if (newstuff) {
    for(int i=0;i<received_bundle_count;i++)
    {
      fprintf(f,"Received %s*/%-16lld @ T%lldms %s\n",
	      received_bundles[i].bid_prefix_hex,
	      received_bundles[i].version,
	      received_bundles[i].rx_time-gettime_ms(),
	      received_bundles[i].reportedP?"":"<fresh>");
      received_bundles[i].reportedP=1;
    }
  }
  return 0;
}
