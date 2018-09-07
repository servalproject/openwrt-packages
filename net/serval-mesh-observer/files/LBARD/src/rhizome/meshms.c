/*
Serval Low-bandwidth asychronous Rhizome Demonstrator.
Copyright (C) 2016 Serval Project Inc.

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
#include <errno.h>
#include <dirent.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#include "sync.h"
#include "lbard.h"

/* Find http api username and password, and also http port for us to connect to.
 */
char server_and_port[1024];
char auth_token[1024];

int meshms_parse_serval_conf()
{
  char serval_conf_file[1024];
  char auth_name[1024]="username";
  char auth_key[1024]="password";
  int port=4110;

  int got_port=0;
  int got_auth=0;

  char *instance_path="/usr/local/etc/serval";
  if (getenv("SERVALINSTANCE_PATH")) instance_path=getenv("SERVALINSTANCE_PATH");
  
  snprintf(serval_conf_file,1024,"%s/serval.conf",instance_path);
  FILE *f=fopen(serval_conf_file,"r");
  char line[1024];
  if (f) {
    line[0]=0; fgets(line,1024,f);
    while(line[0]) {
      if (sscanf(line,"api.restful.users.%[^.].password=%s",
		 auth_name,auth_key)==2) got_auth=1;
      if (sscanf(line,"rhizome.http.port=%d",&port)==1) got_port=1;
      line[0]=0; fgets(line,1024,f);
    }
    fclose(f);
  }

  snprintf(server_and_port,1024,"127.0.0.1:%d",port);
  snprintf(auth_token,1024,"%s:%s",auth_name,auth_key);

  if (!got_auth) fprintf(stderr,"WARNING: Could not find api.restful.users.XXX.password=YYY entry in %s\n",
			 serval_conf_file);
  if (!got_port) fprintf(stderr,"WARNING: Could not find rhizome.http.port=XXX entry in %s\n",
			 serval_conf_file);
  return 0;
}


int meshms_list_conversations(char *sid_hex)
{
  return http_list_meshms_conversations(server_and_port,auth_token,
					sid_hex,30000);
}

int meshms_list_messages(char *sender_sid_hex, char *recipient_sid_hex)
{
  return http_list_meshms_messages(server_and_port,auth_token,
				   sender_sid_hex,recipient_sid_hex,30000);
}

int meshms_send_message(char *sender_sid_hex, char *recipient_sid_hex,
			char *message)
{
  return http_post_meshms(server_and_port,auth_token,message,
			  sender_sid_hex,recipient_sid_hex,30000);
}


int meshms_usage()
{
  fprintf(stderr,"lbard meshms commands:\n"
	  "  lbard meshms list conversations <SID>\n"
	  "  lbard meshms list messages <sender> <recipient>\n"
	  "  lbard meshms send <sender> <recipient> <message>\n");
  exit(-1);
}

int meshms_parse_command(int argc,char **argv)
{
  meshms_parse_serval_conf();
  if (argc<3) exit(meshms_usage());
  if (argc<=7) {
    if (!strcasecmp(argv[2],"list")) {
      if (!strcasecmp(argv[3],"conversations")) {
	if (!argv[4]) exit(meshms_usage());
	if (argc!=5) exit(meshms_usage());
	exit(meshms_list_conversations(argv[4]));
      } else if (!strcasecmp(argv[3],"messages")) {
	if (!argv[4]) exit(meshms_usage());
	if (!argv[5]) exit(meshms_usage());
	if (argc!=6) exit(meshms_usage());
	exit(meshms_list_messages(argv[4],argv[5]));
      } else {
	fprintf(stderr,"Unsupported lbard meshms operation.\n");
	exit(meshms_usage());
      }
    } else if (!strcasecmp(argv[2],"send")) {
      if (!argv[3]) exit(meshms_usage());
      if (!argv[4]) exit(meshms_usage());
      if (!argv[5]) exit(meshms_usage());
      if (argc!=6) exit(meshms_usage());
      exit(meshms_send_message(argv[3],argv[4],argv[5]));
    } else {
      fprintf(stderr,"Unsupported lbard meshms operation.\n");
      exit(meshms_usage());
    }
  }
  exit(meshms_usage());
}

int meshmb_usage()
{
  fprintf(stderr,"lbard meshmb commands:\n"
	  "  lbard meshmb activity <id>\n"
	  "  lbard meshmb find [search term]\n"
	  "  lbard meshmb follow <myid> <peerid>\n"
	  "  lbard meshmb ignore <myid> <peerid>\n"
	  "  lbard meshmb block <myid> <peerid>\n"
	  "  lbard meshmb list following <myid>\n"
	  "  lbard meshmb read <id>\n"
	  "  lbard meshmb send <myid> <message>\n"
	  );
  exit(-1);
}

int meshmb_parse_command(int argc,char **argv)
{
  meshms_parse_serval_conf();
  if (argc<3) exit(meshmb_usage());
  if (argc<=7) {
    if (!strcasecmp(argv[2],"activity")) {
      if (!argv[3]) exit(meshmb_usage());
      exit(http_meshmb_activity(server_and_port,auth_token,argv[3],30000));
    } else if (!strcasecmp(argv[2],"find")) {
      if (!argv[3]) exit(meshmb_usage());
      // exit(http_meshmb_find(argv[3]));
      fprintf(stderr,"meshmb find restful API not implemented.\n");
      exit(-3);
    } else if (!strcasecmp(argv[2],"follow")) {
      if (!argv[3]) exit(meshmb_usage());
      if (!argv[4]) exit(meshmb_usage());
      if (argc!=5) exit(meshmb_usage());
      exit(http_meshmb_follow(server_and_port,auth_token,argv[3],argv[4],30000));
    }
    else if (!strcasecmp(argv[2],"ignore")) {
      if (!argv[3]) exit(meshmb_usage());
      if (!argv[4]) exit(meshmb_usage());
      if (argc!=5) exit(meshmb_usage());
      exit(http_meshmb_follow(server_and_port,auth_token,argv[3],argv[4],30000));
    }
    else if (!strcasecmp(argv[2],"block")) {
      if (!argv[3]) exit(meshmb_usage());
      if (!argv[4]) exit(meshmb_usage());
      if (argc!=5) exit(meshmb_usage());
      exit(http_meshmb_follow(server_and_port,auth_token,argv[3],argv[4],30000));
    }
    else if (!strcasecmp(argv[2],"list")) {
      if (!argv[3]) exit(meshmb_usage());
      if (!argv[4]) exit(meshmb_usage());
      if (!strcmp("following",argv[3]))
	exit(http_meshmb_list_following(server_and_port,auth_token,argv[4],30000));
      exit(meshmb_usage());
    } else if (!strcasecmp(argv[2],"read")) {
      if (!argv[3]) exit(meshmb_usage());
      exit(http_meshmb_read(server_and_port,auth_token,argv[3],30000));
    } else if (!strcasecmp(argv[2],"send")) {
      if (!argv[3]) exit(meshmb_usage());
      if (!argv[4]) exit(meshmb_usage());
      if (argc!=5) exit(meshmb_usage());
      exit(http_meshmb_post(server_and_port,auth_token,argv[3],argv[4],30000));
    } else {
      fprintf(stderr,"Unsupported lbard meshmb operation.\n");
      exit(meshmb_usage());
    }
  }
  exit(meshmb_usage());
}


#define MAX_PERIODIC_REQUESTS 64
int periodic_request_count=0;
char *periodic_request_urls[MAX_PERIODIC_REQUESTS];
char *periodic_request_files[MAX_PERIODIC_REQUESTS];
char periodic_request_output_directory[1024]="/tmp";
int periodic_request_interval=5000; // milliseconds

int register_periodic_request(char *outputfile,char *url)
{
  if (periodic_request_count>=MAX_PERIODIC_REQUESTS) {
    fprintf(stderr,"%s:%d: Too many URLS in periodic request "
	    "configuration. Reduce number or increase "
	    "MAX_PERIODIC_REQUESTS.\n",
	    __FILE__,__LINE__);
    exit(-2);    
  }

  periodic_request_files[periodic_request_count]
    =strdup(outputfile);
  periodic_request_urls[periodic_request_count++]
    =strdup(url);
  
  return 0;
}

long long last_periodic_time=0;

int make_periodic_requests(void)
{
  // Abort if nothing to do (or nothing to do yet)
  if (!periodic_request_count) return 0;
  long long now = gettime_ms();
  if (now<(last_periodic_time+periodic_request_interval))
    return 0;
  last_periodic_time=now;
  
  int i;
  meshms_parse_serval_conf();
  fprintf(stderr,"  About to iterate through %d periodic request URLs\n",
	  periodic_request_count);
  for(i=0;i<periodic_request_count;i++)
    if (periodic_request_urls[i])
      {
	int len=0;
	char url[8192];
	int j;
	fprintf(stderr,"   URL #%d is '%s'\n",i,periodic_request_urls[i]);
	for(j=0;periodic_request_urls[i];j++) {
	  if (periodic_request_urls[i][j]=='$') {
	    // Variable to substitute
	    if (!strncmp("${SID}",&periodic_request_urls[i][j],
			 6)) {
	      fprintf(stderr,"    Substituting ${SID}\n");
	      if (my_sid_hex) {
		strcpy(&url[len],my_sid_hex);
		len+=strlen(my_sid_hex);
	      }
	    } else if (!strncmp("${ID}",&periodic_request_urls[i][j],
				5)) {
	      fprintf(stderr,"    Substituting ${ID}\n");
	      if (my_signingid_hex) {
		strcpy(&url[len],my_signingid_hex);
		len+=strlen(my_signingid_hex);
	      }
	    } else
	      url[len++]=periodic_request_urls[i][j];
	  } else
	    url[len++]=periodic_request_urls[i][j];
	}
	url[len]=0;
	fprintf(stderr,"   Substituted request URL is '%s'\n",
		url);
	if (periodic_request_files[i]) {
	  fprintf(stderr,"   Output filename is '%s'\n",periodic_request_files[i]);
	  FILE *outfile=fopen(periodic_request_files[i],"w");
	  if (outfile) {
	    long long last_read_time=0;
	    fprintf(stderr,"   Making HTTP request\n");
	    int result=http_get_simple(server_and_port,auth_token,
				       url,outfile,3000, // 3 sec timeout
				       &last_read_time,0);
	    fprintf(stderr,"   HTTP result code is %d\n",result);
	    if (result<200||result>209)
	      fprintf(stderr,"%s:%d: HTTP Result of %03d during fetch of '%s'\n",
		      __FILE__,__LINE__,result,periodic_request_urls[i]);
	    fclose(outfile);
	  } else {
	    perror("Could not write to periodic request output file");
	    fprintf(stderr,"%s:%d: Filename was '%s'\n",
		    __FILE__,__LINE__,periodic_request_files[i]);
	  }
	}
      }
  
  return 0;
}

int setup_periodic_requests(char *filename)
{
  FILE *f=fopen(filename,"r");
  if (!f) {
    perror("Could not open periodic RESTful request configuration file.\n");
    exit(-3);
  }

  char line[1024];
  char request[1024];
  char outputfile[1024];  
  line[0]=0; fgets(line,1024,f);
  while(line[0]) {
    if (sscanf(line,"output_directory=%[^\n]",
	       periodic_request_output_directory)==1) {
      ;
    } else if (sscanf(line,"interval=%d",
		      &periodic_request_interval)==1) {
      ;
    } else if (sscanf(line,"request=%[^=]=%[^\n]",
		      outputfile,request)==2) {
      register_periodic_request(outputfile,request);
    } else {
      fprintf(stderr,"%s:%d: Unknown directive in periodic"
	      " RESTful request configuration file: %s\n",
	      __FILE__,__LINE__,line);
      exit(-2);
    }
    line[0]=0; fgets(line,1024,f);
  }

  fclose(f);
  
  return 0;
}
