/*
  Time accounting routines for LBARD.

  These were added in response to problems we were seeing where LBARD would
  stop servicing its web interface (and possibly also the radio), and it
  wasn't immediately clear what the cause was.  GDB or a similar debugger would
  also be able to do this, but we don't have that installed on the Mesh Extenders,
  and also, it isn't practical in the field.  Instead, here we will be able to
  maintain a log of excess time consumption events, and report them, so that we
  can easily find out what is going on, even in the field.

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
#include "code_instrumentation.h"

struct time_excursion {
  char source[32];
  long long duration;
  long long when;
};


#define MAX_TIME_EXCURSIONS 16
#define TIME_EXCURSION_THRESHOLD 250
int recent_count = 0;
struct time_excursion recent[MAX_TIME_EXCURSIONS];
int alltime_count = 0;
struct time_excursion alltime[MAX_TIME_EXCURSIONS];

long long accumulated_time = 0;
long long current_interval_start = 0;
char current_interval_source[32] = "(none)";

int log_time(long long interval, char *source)
{
  int retVal = -1;

  LOG_ENTRY;

  do {

    if (! source) {
      LOG_ERROR("source is null");
      break;
    }

    if (interval < TIME_EXCURSION_THRESHOLD) {
      retVal = 0;
      break;
    }

    // Shuffle down recent time excursions
    int i;
    for (i = MAX_TIME_EXCURSIONS; i > 0; i--) {
      recent[i]=recent[i-1];
    }

    strncpy(recent[0].source, source, sizeof(recent[0].source));

    recent[0].duration = interval;
    recent[0].when = gettime_ms();
    if (recent_count < MAX_TIME_EXCURSIONS) {
      recent_count++;
    }

    // Insert into all time list
    int insert = -1;
    for (i = 0; i < MAX_TIME_EXCURSIONS; i++) {
      if (i < alltime_count) {
        if (alltime[i].duration <= interval) {
          insert=i;
          break; // for
        }
      }
    }

    if (insert < 0) {
      insert = 0;
    }

    for (i = MAX_TIME_EXCURSIONS; i > insert; i--) {
      alltime[i] = alltime[i-1];
    }

    strncpy(alltime[insert].source,source, sizeof(alltime[insert].source));
    alltime[insert].duration = interval;
    alltime[insert].when = gettime_ms();
    if (alltime_count < MAX_TIME_EXCURSIONS) {
      alltime_count++;
    }

  }
  while (0);

  LOG_EXIT;

  return retVal;
}

int account_time_pause()
{  
  LOG_ENTRY;

  accumulated_time += (gettime_ms() - current_interval_start);

  LOG_EXIT;

  return 0;
}

int account_time_resume()
{
  LOG_ENTRY;

  current_interval_start = gettime_ms();

  LOG_EXIT;

  return 0;
}

int account_time(char *source)
{

  LOG_ENTRY;

  if (current_interval_start) {
    // Close of current interval
    long long interval_duration = gettime_ms() - current_interval_start;
    interval_duration += accumulated_time;
    accumulated_time = 0;

    log_time(interval_duration, current_interval_source);
  }

  current_interval_start = gettime_ms();
  accumulated_time = 0;
  strncpy(current_interval_source, source, sizeof(current_interval_source));

  LOG_EXIT;

  return 0;
  
}

int show_time_accounting(FILE *f)
{
  LOG_ENTRY;

  do {

    if (! f) {
      LOG_ERROR("f is null");
      break;
    }

    fprintf(f,
      "<h1>Processor Time accounting</h1>\n"
      "<table><tr><td>\n"

      "<h2>Recent time excursions</h2>\n"
      "<table border=1 padding=2>\n"
      "<tr><th>Function</th><th>Duration</th><th>Time ago</th></tr>\n");

    for(int i = 0; i < recent_count; i++) {
      // KC: QUESTION: should this not be *recent[i].source ? AFAIK, the test below is always true
      if (*recent[i].source) {
        fprintf(f,"<tr><td>%s</td><td>%lld ms</td><td> T-%lldms</td></tr>\n",
          recent[i].source,
          recent[i].duration,
          gettime_ms()-recent[i].when);
      }
    }

    fprintf(f,
      "</table></td>\n"
      "<td><h2>All time longest time excursions</h2>\n"
      "<table border=1 padding=2>\n"
      "<tr><th>Function</th><th>Duration</th><th>Time ago</th>\n");
    
    for(int i = 0;i < alltime_count; i++) {
      // KC: QUESTION: should this not be *alltime[i].source ? AFAIK, the test below is always true
      if (*alltime[i].source) {
        fprintf(f,"<tr><td>%s</td><td>%lld ms</td><td> T-%lldms</td></tr>\n",
          alltime[i].source,
          alltime[i].duration,
          gettime_ms()-alltime[i].when);
      }
    }

    fprintf(f,"</table></td></tr></table>\n");

  }
  while (0);

  LOG_EXIT;

  return 0;
}
