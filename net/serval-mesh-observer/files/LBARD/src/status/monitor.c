/*
Serval Low-Bandwidth Rhizome Transport
Copyright (C) 2015 Serval Project Inc.

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

#include "sync.h"
#include "lbard.h"

int monitor_log_message(char *log,
			char *sender_prefix, char *recipient_prefix,char *msg)
{
  time_t current_time=(long long)time(0);
  long long now_usec=gettime_us();
  struct tm tm;
  localtime_r(&current_time,&tm);

  char filename[1024];

  snprintf(filename,1024,"lbard.monitor.log.%s.txt",log);
  
  FILE *f=fopen(filename,"a");
  fprintf(f,"%04d/%02d/%02d %02d:%02d.%02d:%14lld.%06lld:%s %s %s:%s\n",
	  tm.tm_year+1900,tm.tm_mon,tm.tm_mday+1,
	  tm.tm_hour,tm.tm_min,tm.tm_sec,
	  (long long)current_time,now_usec%1000000,
	  sender_prefix?sender_prefix:"    <no sender>",
	  recipient_prefix?"->":"  ",
	  recipient_prefix?recipient_prefix:"             ",
	  msg);
  fclose(f);
  return 0;
}

int monitor_log(char *sender_prefix, char *recipient_prefix,char *msg)
{
  monitor_log_message("combined",sender_prefix,recipient_prefix,msg);
  if (sender_prefix)
    monitor_log_message(sender_prefix,sender_prefix,recipient_prefix,msg);
  if (recipient_prefix)
    monitor_log_message(recipient_prefix,sender_prefix,recipient_prefix,msg);
  return 0;
}

int bytes_to_prefix(unsigned char *bytes_in,char *prefix_out)
{

  sprintf(prefix_out,"%02X%02X%02X%02X%02X%02X*",
	  bytes_in[0],bytes_in[1],bytes_in[2],
	  bytes_in[3],bytes_in[4],bytes_in[5]);
  return 0;
}
