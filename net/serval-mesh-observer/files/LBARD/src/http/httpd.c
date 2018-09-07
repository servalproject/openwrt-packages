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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sync.h"
#include "lbard.h"

char *inreach_gateway_ip=NULL;
time_t inreach_gateway_time=0;

int urldecode(char *s)
{
  int i,o=0;
  int len=strlen(s);

  for(i=0;i<len;i++) {
    switch (s[i]) {
    case '+':
      s[o++]=' ';
      break;
    case '%':
      {
	int c;
	c=chartohex(toupper(s[i+1]))<<4;
	c|=chartohex(toupper(s[i+2]));
	
	s[o++]=c;
	i+=2;
      }
      break;
    default:
      s[o++]=s[i];
    }
  }
  s[o]=0;
  return o;
  
}

int http_process(struct sockaddr *cliaddr,
		 char *servald_server,char *credential,
		 char *my_sid_hex,
		 int socket)
{
  char buffer[8192];
  char uri[8192];
  int version_major, version_minor;
  int offset;
  int request_len = read(socket,buffer,8192);
  if (debug_http) printf("Read %d bytes of request.\n",request_len);
  int r=sscanf(buffer,"GET %[^ ] HTTP/%d.%d\n%n",
	       uri,&version_major,&version_minor,&offset);
  if (debug_http) {
    printf("  scanned %d fields.\n",r);
    printf("    uri=%s\n",uri);
    printf("    request version=%d.%d\n",version_major,version_minor);
  }
  if (r==3)
    {
      char location[8192]="";
      char message[8192]="";
      if (sscanf(uri,"/submitmessage?location=%[^&]&message=%[^&]",
		    location,message)==2) {
	urldecode(location);
	urldecode(message);
      
	if (debug_http) {
	  printf("  scanned URI fields.\n");
	  printf("    location=[%s]\n",location);
	  printf("    message=[%s]\n",message);
	}
	
	char *m="HTTP/1.0 200 OK\nServer: Serval LBARD\n\nYour message has been submitted.";
	
	// Try to actually send meshms
	// First: compose the string safely.  It might contain UTF-8 text, so we should
	// try to be nice about that.
	{
	  unsigned char combined[8192+8192+1024];
	  snprintf((char *)combined,sizeof(combined),
		   "Message from message_form.html: Location = %s,"
		   " Message as follows: %s",
		   location,message);
	  
	  // XXX - Read recipient
	  {
	    int successful=0;
	    int failed=0;
	    char recipient[1024];
	    
	    FILE *f=fopen("/dos/helpdesk.sid","r");
	    recipient[0]=0; fgets(recipient,1024,f);
	    while(recipient[0]) {
	      // Trim new lines / carriage returns from end of lines.
	      while(recipient[0]&&(recipient[strlen(recipient)-1]<' '))
		recipient[strlen(recipient)-1]=0;
	      
	      int res = http_post_meshms(servald_server,credential,
					 (char *)combined,
					 my_sid_hex,recipient,5000);
	      
	      if (res!=200) {
		m="HTTP/1.0 500 ERROR\nServer: Serval LBARD\n\nYour message could not be submitted.";
		failed++;
	      } else successful++;
	      
	      recipient[0]=0; fgets(recipient,1024,f);
	    }
	    fclose(f);
	  }
	  
	}
	
	write_all(socket,m,strlen(m));
	close(socket);
	return 0;      
      } else if (!strcasecmp(uri,"/inreachgateway/register")) {
	if (inreach_gateway_ip) free(inreach_gateway_ip);
	inreach_gateway_ip=NULL;
	inreach_gateway_ip
	  = strdup(inet_ntoa(((struct sockaddr_in *)cliaddr)->sin_addr));
	inreach_gateway_time=time(0);
	char m[1024];
	snprintf(m,1024,"HTTP/1.0 201 OK\nServer: Serval LBARD\n\n");
	write_all(socket,m,strlen(m));
	close(socket);
	return 0;	
      } else if (!strcasecmp(uri,"/inreachgateway/query")) {
	char m[1024];
	printf("inreach gateway age %lld\n",(long long)(time(0)-inreach_gateway_time));
	if (inreach_gateway_ip&&((time(0)-inreach_gateway_time)<20)) 
	  snprintf(m,1024,"HTTP/1.0 200 OK\nServer: Serval LBARD\nContent-length: %d\n\n%s",
		   (int)strlen(inreach_gateway_ip),inreach_gateway_ip);
	else
	  snprintf(m,1024,"HTTP/1.0 204 OK\nServer: Serval LBARD\n\n");
	write_all(socket,m,strlen(m));
	close(socket);
	return 0;	
      } else if (!strcasecmp(uri,"/js/Chart.min.js")) {
	http_send_file(socket,"/etc/serval/Chart.min.js","text/javascript");
	close(socket);
	return 0;
      } else if (!strcasecmp(uri,"/")) {
	// Display default home page
	// (now uses javascript to show individual parts of the page)
	send_status_home_page(socket);
	close(socket);
	return 0;	
      } else if (!strncasecmp(uri,"/status/",8)) {
	// Report on current peer status
	http_report_network_status(socket,&uri[8]);
	close(socket);
	return 0;	
      } else if (!strcasecmp(uri,"/avacado/testmode1")) {
	system("/sbin/ifconfig adhoc0 down");
	system("rm /serval-var/rhizome.db");
	system("servald stop");
	char m[1024];
	snprintf(m,1024,
		 "HTTP/1.0 200 OK\n"
		 "Server: Serval LBARD\n"
		 "Content-length: 22\n"
		 "\n"
		 "Test Mode #1 selected\n"
		 );
	write_all(socket,m,strlen(m));
	close(socket);
	return 0;	
      } else if (!strncasecmp(uri,"/avacado/renamessid/",20)) {
	char cmd[1024];
	char newssid[32];
	int i,o=0;
	for(i=0;uri[20+i];i++)
	  if ((uri[20+i]>='a'&&uri[20+i]<='z')
	      ||(uri[20+i]>='0'&&uri[20+i]<='9')
	      ||uri[20+i]=='.')
	    if (o<31)
	      newssid[o++]=uri[20+i];
	newssid[o]=0;
	if (o>1) {
	  snprintf(cmd,1024,"sed -e 's/public.servalproject.org/%s/g' < /etc/config/wireless.template > /tmp/wt",newssid);
	  system(cmd);
	  system("mv /tmp/wt /etc/config/wireless.template");
	}
	char m[1024];
	snprintf(m,1024,
		 "HTTP/1.0 200 OK\n"
		 "Server: Serval LBARD\n"
		 "Content-length: 13\n"
		 "\n"
		 "SSID Renamed\n"
		 );
	write_all(socket,m,strlen(m));
	close(socket);
	return 0;	
      } else if (!strcasecmp(uri,"/status.json")) {
	// Report on current peer status
	http_report_network_status_json(socket);
	close(socket);
	return 0;	
      } else {
	// Unknown URL -- Pass to servald on port 4110
	FILE *fsock=fdopen(socket,"w");
	if (fsock) {
	  long long last_read_time=0;
	  int result=http_get_simple(servald_server,NULL,"/",fsock,2000,&last_read_time,1);
	  fclose(fsock);
	  if (result==200) return 0;
	}
      }
    }
  fprintf(stderr,"Saw unknown HTTP request '%s'\n",uri);
  char *m="HTTP/1.0 400 Couldn't parse message\nServer: Serval LBARD\n\n";
  write_all(socket,m,strlen(m));
  close(socket);
  return 0;
}

int http_send_file(int socket,char *filename,char *mime_type)
{
  char m[1024];
  FILE *f=fopen(filename,"r");
  if (!f) {
    snprintf(m,1024,"HTTP/1.0 404 File not found\nServer: Serval LBARD\n\nCould not read file '%s'\n",filename);
    write_all(socket,m,strlen(m));
    return -1;
  }
  struct stat s;
  if (fstat(fileno(f),&s)) {
    snprintf(m,1024,"HTTP/1.0 404 File not found\nServer: Serval LBARD\n\nCould not read file '%s'\n",filename);
    write_all(socket,m,strlen(m));
    return -1;
  }

  int len=s.st_size;
  
  snprintf(m,1024,
	   "HTTP/1.0 200 OK\n"
	   "Server: Serval LBARD\n"
	   "Content-Type: %s\n"
	   "Access-Control-Allow-Origin: *\n"
	   "Access-Control-Allow-Methods: GET\n"
	   "Content-length: %d\n\n",
	   mime_type,
	   len);
  write_all(socket,m,strlen(m));

  char buffer[1024+1];
  int count=fread(buffer,1,1024,f);
  while(count>0) {
    write_all(socket,buffer,count);
    count=fread(buffer,1,1024,f);
  }
  fclose(f);
  return 0;
  
}
