/*
Serval Low-bandwidth asychronous Rhizome Demonstrator.
Copyright (C) 2015-2018 Serval Project Inc.

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

#include "sync.h"
#include "lbard.h"
#include "serial.h"
#include "version.h"
#include "radios.h"

int log_rssi(struct peer_state *p,int rssi)
{
  // Shuffle old entries down
  int i;
  for(i=RSSI_LOG_SIZE-1;i>0;i--)
    {
      p->recent_rssis[i]=p->recent_rssis[i-1];
      p->recent_rssi_times[i]=p->recent_rssi_times[i-1];
    }
  if (p->rssi_log_count<RSSI_LOG_SIZE)
    p->rssi_log_count++;

  // Put new entry at top
  p->recent_rssis[0]=rssi;
  p->recent_rssi_times[0]=gettime_ms();
  return 0;
}

int log_rssi_timewarp(long long delta)
{
  for(int p=0;p<peer_count;p++)
    {
      if (!peer_records[p]) continue;
      for(int i=0;i<peer_records[p]->rssi_log_count;i++) {
	peer_records[p]->recent_rssi_times[i]+=delta;
      }
    }
  return 0;
}

int log_rssi_graph(FILE *f,struct peer_state *p)
{
  /*
    Draw a graph showing the recent RSSI strength of each peer.
    We only show the last 60 seconds worth, even if we have a longer
    time series.
    We normalise the values to fit the range 56-255, i.e., a range of 200 "units",
    which are not currently in dBm or similar. We should change this later.
    Also would be nice to know the actual noise floor as well, so that we can show
    the link margin, since that is really what we care about.
  */
  fprintf(f,
	  "<canvas id=\"peer%p\" width=\"240\" height=\"64\"></canvas>\n"
	  "<script>\n"
	  "var ctx = document.getElementById(\"peer%p\");\n",
	  p,p);
  fprintf(f,
	  "var theData = {\n"
	  "  labels: [");
  for(int i=59;i>0;i--) fprintf(f,"%d,",i); fprintf(f,"0],\n");

  int bins[60];
  for(int i=0;i<60;i++) bins[i]=0;
  long long now=gettime_ms();
  for(int i=0;i<p->rssi_log_count;i++)
    {
      if (p->recent_rssi_times[i]>(now-60000)) {
	long long agems=now-p->recent_rssi_times[i];
	int x=60-(agems/1000);
	if (x>=0&&x<=60)
	  {
	    if (p->recent_rssis[i]>bins[x]) bins[x]=p->recent_rssis[i];
	  }
      }
    }

  fprintf(f,"datasets: [{\n"
	  "label: 'RSSI',\n"
	  "backgroundColor: '#0000ff',\n"
	  "borderColor: '#0000ff',\n"
	  "borderWidth: 1,\n"
	  "data: [");
  for(int i=0;i<60;i++) fprintf(f,"%d,",bins[i]);
  fprintf(f,"]}]};\n");

  
  fprintf(f,
	  "var theOptions = { scales: { xAxes: [ { display: false, barPercentage: 1, categoryPercentage: 1 }]}, legend: { display: false}, animation: { duration: 0 }, hover: { animationDuration: 0}, responsiveAnimationDuration: 0, elements: { line: { tension: 0, } } };\n"
	  "var myChart = new Chart.Bar(ctx, { type: 'bar', data: theData , options: theOptions });\n");
  fprintf(f,"</script>\n");
	  
  return 0;

}
