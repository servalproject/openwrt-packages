#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>

#include "sync.h"
#include "lbard.h"
#include "serial.h"

struct json_parse_state {
  int parse_state;
  int on_new_line;
  int depth;
  int pending_colon;
  int line_len;
#define MAX_LINE_LEN 1024
  char line[MAX_LINE_LEN];
  int columns;
  int meshms_message;
  int row_count;
};

int json_render_meshms_message(struct json_parse_state *p)
{
  char *field[10];
  for(int i=0;i<9;i++)
    field[i]=strtok(i?NULL:p->line,":");
  if (!field[8]) return -1;
  long long age=(gettime_ms()/1000)-strtoll(field[8],NULL,10);

  printf("%d:%s:%lld:%s:",
	 p->row_count++,field[3],age,field[0]);
  for(int i=0;field[5][i];i++) {
    if (field[5][i]=='%') {
      char hex[3]={field[5][i+1],field[5][i+2],0};
      printf("%c",(int)strtoll(hex,NULL,16));
      i+=2;
    } else printf("%c",field[5][i]);
  }
  printf("\n");

  return 0;
}

int json_new_line(struct json_parse_state *p)
{
  p->line[p->line_len]=0;
 if (!p->on_new_line) {
    if (p->line_len&&(p->columns>1)) {
      p->line[p->line_len]=0;
      if (!strcmp(p->line,"type:my_sid:their_sid:offset:token:text:delivered:read:timestamp:ack_offset")) {
	p->meshms_message=1;
	printf("_id:offset:age:type:message\n");
      } else {
	if (!p->meshms_message)
	  printf("%s\n",p->line);
	else json_render_meshms_message(p);
      }
    }
    p->on_new_line=1;
    p->line_len=0;
    p->line[0]=0;
    p->columns=0;
  }
  return 0;
}

int json_flatten(struct json_parse_state *p,char *body, int body_len)
{
  for(int i=0;i<body_len;i++) {
    if (p->line_len>(MAX_LINE_LEN-3)) p->line_len=(MAX_LINE_LEN-3);
    switch(p->parse_state) {
    case 0: // normal
      switch(body[i]) {
      case ':': // object class
	json_new_line(p);
	break;
      case ',': // field separator
	p->pending_colon=1;
	p->columns++;
	break;
      case '\"':
	p->parse_state=1;
	break;
      case '\\':
	p->parse_state=2;
	break;
      case '[': case '{':
	// array element
	json_new_line(p);
	p->depth++;
	break;
      case ']': case '}':
	// array element
	p->depth--;
	json_new_line(p);
	if (p->depth<1) return 1;
	break;
      case '\r': case '\n':
	json_new_line(p);
	break;
      default:
	// normal character
	if (!p->on_new_line)
	  if (p->pending_colon)
	    p->line[p->line_len++]=':';
	p->pending_colon=0;
	if ((body[i]!=':')&&(body[i]!='%'))
	  p->line[p->line_len++]=body[i];
        else
	  {
	    p->line[p->line_len++]='%';
	    p->line[p->line_len++]=hextochar(body[i]>>4);
	    p->line[p->line_len++]=hextochar(body[i]&0xf);
	  }
	p->on_new_line=0;
	break;
      }
      break;
    case 1: // inside quotes
      switch(body[i]) {
      case '\"': p->parse_state=0;	break;
      case '\\': p->parse_state=3; break;
      default:
	// normal character
	if (!p->on_new_line)
	  if (p->pending_colon)
	    p->line[p->line_len++]=':';
	p->pending_colon=0;
	if ((body[i]!=':')&&(body[i]!='%'))
	  p->line[p->line_len++]=body[i];
        else
	  {
	    p->line[p->line_len++]='%';
	    p->line[p->line_len++]=hextochar(body[i]>>4);
	    p->line[p->line_len++]=hextochar(body[i]&0xf);
	  }
	p->on_new_line=0;
	break;	
      }
      break;
    case 2: // following a "\"
      p->line[p->line_len++]=body[i];
      p->on_new_line=0;
      p->parse_state=0;
      break;
    case 3: // following a \ inside quotes
      p->line[p->line_len++]=body[i];
      p->on_new_line=0;
      p->parse_state=1;
      break;
    }
  }
  return 0;
}

int json_body(int sock,long long timeout_time)
{
  // Now output the JSON lines
  struct json_parse_state parse_state;
  bzero(&parse_state, sizeof(parse_state));
  
  while(1) {
    char line[1024];
    int r=read_nonblock(sock,line,1024);
    if (r>0) {
      if (json_flatten(&parse_state,line,r)) break;
    } else usleep(1000);
    if (gettime_ms()>timeout_time) {
      // Quit on timeout
      close(sock);
      return -1;
    }
  }  
  json_new_line(&parse_state);
  close(sock);
  return 0;
}

int connect_to_port(char *host,int port)
{
  struct hostent *hostent;
  hostent = gethostbyname(host);
  if (!hostent) {
    return -1;
  }

  struct sockaddr_in addr;  
  addr.sin_family = AF_INET;     
  addr.sin_port = htons(port);   
  addr.sin_addr = *((struct in_addr *)hostent->h_addr);
  bzero(&(addr.sin_zero),8);     

  int sock=socket(AF_INET, SOCK_STREAM, 0);
  if (sock==-1) {
    perror("Failed to create a socket.");
    return -1;
  }

  if (connect(sock,(struct sockaddr *)&addr,sizeof(struct sockaddr)) == -1) {
    // perror("connect() to port failed");
    close(sock);
    return -1;
  }
  return sock;
}

int num_to_char(int n)
{
  assert(n>=0); assert(n<64);
  if (n<26) return 'A'+(n-0);
  if (n<52) return 'a'+(n-26);
  if (n<62) return '0'+(n-52);
  switch(n) {
  case 62: return '+'; 
  case 63: return '/';
  default: return -1;
  }
}

int base64_append(char *out,int *out_offset,unsigned char *bytes,int count)
{
  int i;
  for(i=0;i<count;i+=3) {
    int n=4;
    unsigned int b[30];
    b[0]=bytes[i];
    if ((i+2)>=count) { b[2]=0; n=3; } else b[2]=bytes[i+2];
    if ((i+1)>=count) { b[1]=0; n=2; } else b[1]=bytes[i+1];
    out[(*out_offset)++] = num_to_char((b[0]&0xfc)>>2);
    out[(*out_offset)++] = num_to_char( ((b[0]&0x03)<<4) | ((b[1]&0xf0)>>4) );
    if (n==2) {
      out[(*out_offset)++] = '=';
      out[(*out_offset)++] = '=';
      return 0;
    }
    out[(*out_offset)++] = num_to_char( ((b[1]&0x0f)<<2) | ((b[2]&0xc0)>>6) );
    if (n==3) {
      out[(*out_offset)++] = '=';
      return 0;
    }
    out[(*out_offset)++] = num_to_char((b[2]&0x3f)>>0);
  }
  return 0;
}

int http_get_simple(char *server_and_port, char *auth_token,
		    char *path, FILE *outfile, int timeout_ms,
		    long long *last_read_time, int outputHeaders)
{
  // Send simple HTTP request to server, and write result into outfile.

  char server_name[1024];
  int server_port=-1;

  if (sscanf(server_and_port,"%[^:]:%d",server_name,&server_port)!=2) return -1;

  long long timeout_time=gettime_ms()+timeout_ms;
  
  if (auth_token&&strlen(auth_token)>500) return -1;
  if (strlen(path)>500) return -1;
  
  char request[2048];
  char authdigest[1024];
  int zero=0;

  if (auth_token) {
    bzero(authdigest,1024);
    base64_append(authdigest,&zero,(unsigned char *)auth_token,strlen(auth_token));
  }

  // Build request
  if (auth_token)
    snprintf(request,2048,
	     "GET %s HTTP/1.1\n"
	     "Authorization: Basic %s\n"
	     "Host: %s:%d\n"
	     "Accept: */*\n"
	     "\n",
	     path,
	     authdigest,
	     server_name,server_port);
  else
    snprintf(request,2048,
	     "GET %s HTTP/1.1\n"
	     "Host: %s:%d\n"
	     "Accept: */*\n"
	     "\n",
	     path,
	     server_name,server_port);
    
  
  int sock=connect_to_port(server_name,server_port);
  if (sock<0) return -1;

  write_all(sock,request,strlen(request));

  // Read reply, streaming output to file after we have skipped the header
  int http_response=-999;
  #define LINE_BYTES 65536
  char line[LINE_BYTES];
  int len=0;
  int empty_count=0;
  int content_length=-1;
  set_nonblock(sock);
  int r;
  while(len<1024) {
    r=read_nonblock(sock,&line[len],1);
    if (r==1) {
      if (outputHeaders) fputc(line[len],outfile);
      if ((line[len]=='\n')||(line[len]=='\r')) {
	if (len) empty_count=0; else empty_count++;
	line[len+1]=0;
	if (sscanf(line,"Content-Length: %d",&content_length)==1) {
	  // got content length
	  // fprintf(stderr,"HTTP Content-Length = %d\n",content_length);
	}
	if (sscanf(line,"HTTP/1.0 %d",&http_response)==1) {
	  // got http response
	  // fprintf(stderr,"HTTP Response = %d\n",http_response);
	}
	if (sscanf(line,"HTTP/1.1 %d",&http_response)==1) {
	  // got http response
	  // fprintf(stderr,"HTTP Response = %d\n",http_response);
	}
	len=0;
	// Have we found end of headers?
	if (empty_count==3) break;
      } else len++;
    } else usleep(1000);
    if (gettime_ms()>timeout_time) {
      // If still in header, just quit on timeout
      close(sock);
      return -1;
    }
  }

  // Got headers, read body and write to file
  // fprintf(stderr,"  reading body...\n");

  int rxlen=0;
  r=0;
  while(r>-1) {
    errno=0;
    r=read_nonblock(sock,line,LINE_BYTES);
    if (r>0) {
      // fprintf(stderr,"read %d body bytes @ T%lld\n",r,timeout_time-gettime_ms());
      if (last_read_time) *last_read_time=gettime_ms();
      int written=fwrite(line,1,r,outfile);      
      if (written!=r) {
	fprintf(stderr,"Short write of HTTP data to file: %d of %d bytes\n",written,r);
	close(sock);
	return -1;
      }
      fflush(outfile);
      rxlen+=r;
      if (content_length>-1) {
	if (rxlen>=content_length) break;
      }
    } else {
      // If no error and no data, then it really is EOF
      if (!errno) break;
      // ... else, wait a little while, and try again.
      usleep(10000);
    }

    if (gettime_ms()>timeout_time) {
      fprintf(stderr,"HTTP read timeout (read %d of %d bytes)\n",
	      rxlen,content_length);
      close(sock);
      return -1;
    }
    
  }
  
  close(sock);
  {
    struct stat s;
    int r=fstat(fileno(outfile),&s);
    if (0) fprintf(stderr,"  HTTP download file: length=%lld, stat result=%d\n",
		   (long long)s.st_size,r);
    if (errno) perror("fstat");
    if (s.st_size<content_length) {
      fprintf(stderr,"  HTTP download file is too short. Returning error.\n");
      return -1;
    }

  }
  
  return http_response;
}

int http_post_bundle(char *server_and_port, char *auth_token,
		     char *path,
		     unsigned char *manifest_data, int manifest_length,
		     unsigned char *body_data, int body_length,
		    int timeout_ms)
{

  char server_name[1024];
  int server_port=-1;

  // Limit bundle size to 5MB via this transport, to limit memory consumption.
  if (body_length>(5*1024*1024)) return -1;
  
  if (sscanf(server_and_port,"%[^:]:%d",server_name,&server_port)!=2) return -1;

  long long timeout_time=gettime_ms()+timeout_ms;
  
  if (strlen(auth_token)>500) return -1;
  if (strlen(path)>500) return -1;
  
  char request[8192+body_length];
  char authdigest[1024];
  int zero=0;

  bzero(authdigest,1024);
  base64_append(authdigest,&zero,(unsigned char *)auth_token,strlen(auth_token));

  // Generate random content dividor token
  unsigned long long unique,unique2;
  unique=random(); unique=unique<<32; unique|=random();
  unique2=random(); unique2=unique2<<32; unique2|=random();
  
  char manifest_header[1024];
  snprintf(manifest_header,1024,
	   "Content-Disposition: form-data; name=\"manifest\"\r\n"
	   "Content-Length: %d\r\n"
	   "Content-Type: rhizome/manifest; format=text+binarysig\r\n"
	   "\r\n", manifest_length);
  char body_header[1024];
  snprintf(body_header,1024,
	   "Content-Disposition: form-data; name=\"payload\"\r\n"
	   "Content-Length: %d\r\n"
	   "Content-Type: binary/data\r\n"
	   "\r\n",
	   body_length);

  char boundary_string[1024];
  snprintf(boundary_string,1024,"------------------------%016llx%016llx",
	   unique,unique2);
  int boundary_len=strlen(boundary_string);

  // Calculate content length
  
  // Build request
  int total_len=0;

  int content_length=
    // manifest part
    2+boundary_len+2
    +strlen(manifest_header)
    +manifest_length+2

    // payload part
    +2+boundary_len+2
    +strlen(body_header)
    +body_length+2
    +2+boundary_len+2

    // Stuff we shouldn't need???
    +2;   // not sure where we have missed this last 2, but it is needed to reconcile

  // XXX - Work around a nasty bug in servald's HTTP request parser
  char *variable_length_string="";
  if ((content_length%8192)>=7870)
    variable_length_string="X-Variable-length-header-to-work-around-serval-dna-http-bug-that-fails-to-recognise-end-boundary-string-if-it-crosses-an-8kb-boundary-in-the-http-stream: The sole purpose of this header line is to grow the HTTP request part sufficiently, that the boundary string following the body will be pushed entirely into the next 8KB block\n";	 
  
  int header_length = snprintf(request,8192,
			       "POST %s HTTP/1.1\r\n"
			       "Authorization: Basic %s\r\n"
			       "Host: %s:%d\r\n"
			       "Content-Length: %d\r\n"
			       "Accept: */*\r\n"
			       "%s"
			       "Content-Type: multipart/form-data; boundary=%s\r\n"
			       "\r\n",
			       path,
			       authdigest,
			       server_name,server_port,
			       content_length,
			       variable_length_string,
			       boundary_string);
  
  int extra_length = snprintf(&request[header_length],8192,
			      "--%s\r\n"
			      "%s",
			      boundary_string,
			      manifest_header);
  
  total_len=header_length+extra_length;
  bcopy(manifest_data,&request[total_len],manifest_length);

  int subtotal_len=total_len;
  total_len=total_len+manifest_length;
  total_len+=snprintf(&request[total_len],8192+body_length-total_len,  
			   "\r\n"
			   "--%s\r\n"
			   "%s",
			   boundary_string,
			   body_header);
  bcopy(body_data,&request[total_len],body_length);
  total_len=total_len+body_length;
  total_len+=snprintf(&request[total_len],8192+body_length-total_len,
	   "\r\n"
	   "--%s--\r\n",
	   boundary_string);

  if (0) fprintf(stderr,"  content_length was calculated at %d bytes, total_len=%d\n",
		 content_length,total_len);
  int present_len=2+boundary_len+2+strlen(manifest_header);
  if  (0) fprintf(stderr,
		  "    subtotal_len=%d, difference+present=%d (should match content_length)\n",
		  subtotal_len,total_len-subtotal_len+present_len);
  
  int sock=connect_to_port(server_name,server_port);
  if (sock<0) return -1;

  // Write request
  write_all(sock,request,total_len);

  // Read reply, streaming output to file after we have skipped the header
  int http_response=-1;
  char line[1024];
  int len=0;
  int empty_count=0;
  set_nonblock(sock);
  int r;
  while(len<1024) {
    r=read_nonblock(sock,&line[len],1);
    if (r==1) {
      if ((line[len]=='\n')||(line[len]=='\r')) {
	if (len) empty_count=0; else empty_count++;
	line[len+1]=0;
	// if (len) printf("Line of response: %s\n",line);
	if (sscanf(line,"HTTP/1.0 %d",&http_response)==1) {
	  // got http response
	  if (http_response<200 || http_response > 209)
	    fprintf(stderr,"HTTP Error: %s\n     (URL: '%s')\n",line,path);
	}
	if (sscanf(line,"HTTP/1.1 %d",&http_response)==1) {
	  // got http response
	  if (http_response<200 || http_response > 209)
	    fprintf(stderr,"HTTP Error: %s\n     (URL: '%s')\n",line,path);
	}
	len=0;
	// Have we found end of headers?
	if (empty_count==3) break;
      } else len++;
    } else usleep(1000);
    if (gettime_ms()>timeout_time) {
      // If still in header, just quit on timeout
      close(sock); 
      return -1;
    }
  }
  close(sock);
  return http_response;  
}

int http_post_meshms_common(char *server_and_port, char *auth_token,
			    char *message,char *sender,char *recipient,
			    int timeout_ms,int meshmsP)
{
  
  char server_name[1024];
  int server_port=-1;

  int message_length=strlen(message);
  
  if (sscanf(server_and_port,"%[^:]:%d",server_name,&server_port)!=2) return -1;

  long long timeout_time=gettime_ms()+timeout_ms;
  
  if (strlen(auth_token)>500) return -1;
  
  char request[8192+message_length];
  char authdigest[1024];
  int zero=0;

  bzero(authdigest,1024);
  base64_append(authdigest,&zero,(unsigned char *)auth_token,strlen(auth_token));

  // Generate random content dividor token
  unsigned long long unique;
  unique=random(); unique=unique<<32; unique|=random();
  
  char message_header[1024];
  snprintf(message_header,1024,
	   "Content-Disposition: form-data; name=\"message\"\r\n"
	   "Content-Length: %d\r\n"
	   "Content-Type: text/plain; charset=utf-8\r\n"
	   "\r\n", message_length);

  char boundary_string[1024];
  snprintf(boundary_string,1024,"------------------------%016llx",unique);
  int boundary_len=strlen(boundary_string);

  // Calculate content length
  int content_length=0
    +2+boundary_len+2
    +strlen(message_header)
    +message_length+2
    +2+boundary_len+2
    +2;   // not sure where we have missed this last 2, but it is needed to reconcile
  
  // Build request
  char url[8192];
  snprintf(url,8192,"/restful/meshm%c/%s%s%s/sendmessage",
	   meshmsP?'s':'b',
	   sender,
	   meshmsP?"/":"",
	   meshmsP?recipient:"");
	   
  
  int total_len = snprintf(request,8192,
			   "POST %s HTTP/1.1\r\n"
			   "Authorization: Basic %s\r\n"
			   "Host: %s:%d\r\n"
			   "Content-Length: %d\r\n"
			   "Accept: */*\r\n"
			   "Content-Type: multipart/form-data; boundary=%s\r\n"
			   "\r\n"
			   "--%s\r\n"
			   "%s",
			   url,
			   authdigest,
			   server_name,server_port,
			   content_length,
			   boundary_string,
			   boundary_string,
			   message_header);
  bcopy(message,&request[total_len],message_length);

  int subtotal_len=total_len;
  total_len=total_len+message_length;
  total_len+=snprintf(&request[total_len],8192-total_len,
	   "\r\n"
	   "--%s--\r\n",
	   boundary_string);

  if (0) fprintf(stderr,"  content_length was calculated at %d bytes, total_len=%d\n",
		 content_length,total_len);
  int present_len=2+boundary_len+2+strlen(message_header);
  if (0) fprintf(stderr,
		 "    subtotal_len=%d, difference+present=%d (should match content_length)\n",
		 subtotal_len,total_len-subtotal_len+present_len);

  //  fprintf(stderr,"Request:\n%s\n",request);
  
  int sock=connect_to_port(server_name,server_port);
  if (sock<0) return -1;

  // Write request
  write_all(sock,request,total_len);

  // Read reply, streaming output to file after we have skipped the header
  int http_response=-1;
  char line[1024];
  int len=0;
  int empty_count=0;
  set_nonblock(sock);
  int r;
  while(len<1024) {
    r=read_nonblock(sock,&line[len],1);
    if (r==1) {
      if ((line[len]=='\n')||(line[len]=='\r')) {
	if (len) empty_count=0; else empty_count++;
	line[len+1]=0;
	// if (len) printf("Line of response: %s\n",line);
	if (sscanf(line,"HTTP/1.0 %d",&http_response)==1) {
	  // got http response
	  if (http_response<200 || http_response > 209)
	    fprintf(stderr,"HTTP Error: %s\n     (URL: '%s')\n",line,url);
	  
	}
	if (sscanf(line,"HTTP/1.1 %d",&http_response)==1) {
	  // got http response
	  if (http_response<200 || http_response > 209)
	    fprintf(stderr,"HTTP Error: %s\n     (URL: '%s')\n",line,url);
	}
	len=0;
	// Have we found end of headers?
	if (empty_count==3) break;
      } else len++;
    } else usleep(1000);
    if (gettime_ms()>timeout_time) {
      // If still in header, just quit on timeout
      close(sock);
      return -1;
    }
  }
  close(sock);
  return http_response;
  
}

int http_meshmb_post(char *server_and_port, char *auth_token,
		     char *sender,char *message,
		     int timeout_ms)
{
  return http_post_meshms_common(server_and_port,auth_token,
				 message,sender,NULL,
				 timeout_ms,0 /* not meshms, but meshmb */);  
}

int http_post_meshms(char *server_and_port, char *auth_token,
		     char *message,char *sender,char *recipient,
		     int timeout_ms)
{
  return http_post_meshms_common(server_and_port,auth_token,
				 message,sender,recipient,
				 timeout_ms,1 /* is meshms, not meshmb */);
  
}


int http_json_request(char *server_and_port, char *auth_token,
		      int timeout_ms,char *url,char *request_type)
{  
  char server_name[1024];
  int server_port=-1;
  
  if (sscanf(server_and_port,"%[^:]:%d",server_name,&server_port)!=2)
    {
      fprintf(stderr,"Could not parse server_and_port\n");
      return -1;
    }

  long long timeout_time=gettime_ms()+timeout_ms;
  
  if (strlen(auth_token)>500)
    {
      fprintf(stderr,"Auth token too long\n");
      return -1;
    }
  
  char request[8192];
  char authdigest[1024];
  int zero=0;
  
  bzero(authdigest,1024);
  base64_append(authdigest,&zero,(unsigned char *)auth_token,strlen(auth_token));

  // Generate random content dividor token
  unsigned long long unique;
  unique=random(); unique=unique<<32; unique|=random();
    
  // Build request
  int total_len = snprintf(request,8192,
			   "%s %s HTTP/1.1\r\n"
			   "Authorization: Basic %s\r\n"
			   "Host: %s:%d\r\n"
			   "Content-Length: 0\r\n"
			   "Accept: */*\r\n"
			   "%s"
			   "\r\n",
			   request_type,
			   url,
			   authdigest,
			   server_name,server_port,
			   !strcmp(request_type,"POST")?"Content-Type: text/plain; charset=UTF-8\r\n":""
			   );

  // fprintf(stderr,"Request:\n%s\n",request);
  
  int sock=connect_to_port(server_name,server_port);
  if (sock<0) {
    fprintf(stderr,"Could not open socket to servald\n");
    return -1;
  }

  // Write request
  write_all(sock,request,total_len);

  // Read reply, streaming output to file after we have skipped the header
  int http_response=-999;
  char line[1024];
  int len=0;
  int empty_count=0;
  set_nonblock(sock);
  int r;
  while(len<1024) {
    r=read_nonblock(sock,&line[len],1);
    if (r==1) {
      if ((line[len]=='\n')||(line[len]=='\r')) {
	if (len) empty_count=0; else empty_count++;
	line[len+1]=0;
	// if (len) printf("Line of response: %s\n",line);
	if (sscanf(line,"HTTP/1.0 %d",&http_response)==1) {
	  // got http response
	  // fprintf(stderr,"  HTTP response from servald is: %d\n",http_response);
	  if (http_response<200 || http_response > 209)
	    fprintf(stderr,"HTTP Error: %s\n     (URL: '%s')\n",line,url);
	}
	if (sscanf(line,"HTTP/1.1 %d",&http_response)==1) {
	  // got http response
	  // fprintf(stderr,"  HTTP response from servald is: %d\n",http_response);
	  if (http_response<200 || http_response > 209)
	    fprintf(stderr,"HTTP Error: %s\n     (URL: '%s')\n",line,url);
	}
	len=0;
	// Have we found end of headers?
	if (empty_count==3) break;
      } else len++;
    } else usleep(1000);
    if (gettime_ms()>timeout_time) {
      // If still in header, just quit on timeout
      close(sock);
      fprintf(stderr,"Premature end of data while reading HTTP headers\n");
      return -1;
    }
  }

  if (http_response>=200 && http_response <= 209)
    json_body(sock,timeout_time);  

  return http_response;  
}


int http_list_meshms_conversations(char *server_and_port, char *auth_token,
				   char *participant,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshms/%s/conversationlist.json",
	   participant);
  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"GET");  
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_list_meshms_messages(char *server_and_port, char *auth_token,
			      char *sender, char *recipient,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshms/%s/%s/messagelist.json",
	   sender,recipient);
  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"GET");    
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_activity(char *server_and_port, char *auth_token,
			 char *id_hex,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/activity.json",id_hex);

  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"GET");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_activity_since(char *server_and_port,
			       char *auth_token,
			       char *id_hex,char *token,
			       int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/activity/%s/activity.json",
	   id_hex,token);

  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"GET");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_follow(char *server_and_port, char *auth_token,
		       char *me_hex,char *you_hex,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/follow/%s",
	   me_hex,you_hex);

  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"POST");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_ignore(char *server_and_port, char *auth_token,
		       char *me_hex,char *you_hex,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/ignore/%s",
	   me_hex,you_hex);

  int result=http_json_request(server_and_port,auth_token,
			   timeout_ms,url,"POST");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_block(char *server_and_port, char *auth_token,
		       char *me_hex,char *you_hex,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/block/%s",
	   me_hex,you_hex);

  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"POST");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_list_following(char *server_and_port,
			       char *auth_token,
			       char *id_hex,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/feedlist.json",
	   id_hex);

  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"GET");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_meshmb_read(char *server_and_port,
		     char *auth_token,
		     char *id_hex,int timeout_ms)
{
  char url[8192];
  snprintf(url,8192,"/restful/meshmb/%s/messagelist.json",
	   id_hex);

  int result=http_json_request(server_and_port,auth_token,
			       timeout_ms,url,"GET");
  if (result<200||result>204) fprintf(stderr,"ERROR: HTTP Response was %d\n",result);
  return result;
}

int http_get_async(char *server_and_port, char *auth_token,
		   char *path, int timeout_ms)
{
  // Send simple HTTP request to server, and return socket or -1 when we have parsed
  // the headers.

  char server_name[1024];
  int server_port=-1;

  if (sscanf(server_and_port,"%[^:]:%d",server_name,&server_port)!=2) return -1;

  long long timeout_time=gettime_ms()+timeout_ms;
  
  if (strlen(auth_token)>500) return -1;
  if (strlen(path)>500) return -1;
  
  char request[2048];
  char authdigest[1024];
  int zero=0;
  
  bzero(authdigest,1024);
  base64_append(authdigest,&zero,(unsigned char *)auth_token,strlen(auth_token));

  // Build request
  snprintf(request,2048,
	   "GET %s HTTP/1.1\n"
	   "Authorization: Basic %s\n"
	   "Host: %s:%d\n"
	   "Accept: */*\n"
	   "\n",
	   path,
	   authdigest,
	   server_name,server_port);
  
  int sock=connect_to_port(server_name,server_port);
  if (sock<0) return -1;

  write_all(sock,request,strlen(request));

  // Read reply, streaming output to file after we have skipped the header
  int http_response=-1;
  #define LINE_BYTES 65536
  char line[LINE_BYTES];
  int len=0;
  int empty_count=0;
  int content_length=-1;
  int header_bytes=0;
  set_nonblock(sock);
  int r;
  while(len<1024) {
    header_bytes++;
    r=read_nonblock(sock,&line[len],1);
    if (r==1) {
      if ((line[len]=='\n')||(line[len]=='\r')) {
	if (len) empty_count=0; else empty_count++;
	line[len+1]=0;
	if (sscanf(line,"Content-Length: %d",&content_length)==1) {
	  // got content length
	}
	if (sscanf(line,"HTTP/1.0 %d",&http_response)==1) {
	  // got http response
	}
	if (sscanf(line,"HTTP/1.1 %d",&http_response)==1) {
	  // got http response
	}
	len=0;
	// Have we found end of headers?
	if (empty_count==3) break;
      } else len++;
    } else usleep(1000);
    if (gettime_ms()>timeout_time) {
      // If still in header, just quit on timeout
      close(sock);
      return -1;
    }
  }

  // Got headers
  if (0) printf("Read %d header bytes (response code %d). Ready for async fetch.\n",
		header_bytes,http_response);
  return sock;
}

int http_read_next_line(int sock, char *line, int *len, int maxlen)
{
  set_nonblock(sock);
  int r;
  while((*len)<maxlen) {
    errno=0;
    r=read_nonblock(sock,&line[*len],1);
    if (r==1) {
      if ((line[*len]=='\n')||(line[*len]=='\r')) {
	line[(*len)+1]=0;
	*len=0;
	// Got a line
	return 0;
      } else (*len)++;
    } else
      // Not enough data for a full line yet
      return -1;
    if ((!r)&&(!errno)) {
      // End of connection
      close(sock);
      return 1;
    }
  }

  // Over-long line: truncate and return
  *len=0;
  line[maxlen-1]=0;
  return 0;
}

