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
#include <strings.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <assert.h>
#include <sys/stat.h>

#include "sync.h"
#include "lbard.h"

int _report_file(const char *filename,const char *file,
		 const int line,const char *function)
{
  if (!filename) return 0;
  
  struct stat s;
  int r=stat(filename,&s);

  fprintf(stderr,"%s:%d:%s() : File '%s' length=%lld, result=%d\n",
	  file,line,function,filename,(long long)s.st_size,r);
  char cmd[1024];
  snprintf(cmd,1024,"ls -lad %s 1>&2",filename);
  system(cmd);
  return 0;
}

char *bid_of_cached_bundle=NULL;
long long cached_version=0;
int cached_manifest_len=0;
unsigned char *cached_manifest=NULL;
int cached_manifest_encoded_len=0;
unsigned char *cached_manifest_encoded=NULL;
int cached_body_len=0;
unsigned char *cached_body=NULL;

int prime_bundle_cache(int bundle_number,char *sid_prefix_hex,
		       char *servald_server, char *credential)
{
  if (bundle_number<0) return -1;

  for(int i=0;i<6;i++) {
    if (sid_prefix_hex[i]<'0'||sid_prefix_hex[i]>'f') {
      fprintf(stderr,"Saw illegal character 0x%02x in sid_prefix_hex[%d]\n",
	      (unsigned char)sid_prefix_hex[i],i);
      exit(-1);
    }
  }
  
  if ((!bid_of_cached_bundle)
      ||strcasecmp(bundles[bundle_number].bid_hex,bid_of_cached_bundle)
      ||(cached_version!=bundles[bundle_number].version)
      ) {
    // Cache is invalid - release
    if (bid_of_cached_bundle) {
      free(bid_of_cached_bundle); bid_of_cached_bundle=NULL;
      free(cached_manifest); cached_manifest=NULL;
      free(cached_manifest_encoded); cached_manifest_encoded=NULL;
      free(cached_body); cached_body=NULL;
    }

    // Load bundle into cache
    char path[8192];
    char filename[1024];
    
    snprintf(path,8192,"/restful/rhizome/%s.rhm",
	     bundles[bundle_number].bid_hex);

    long long t1=gettime_ms();

    char pathbuf[1024];
    snprintf(filename,1024,"%s/%d.%s.manifest",getcwd(pathbuf,1024),getpid(),
	     sid_prefix_hex);
      
    unlink(filename);
    FILE *f=fopen(filename,"w");
    if (!f) {
      fprintf(stderr,"could not open output file '%s'.\n",filename);
      perror("fopen");
      return -1;
    }
    int result_code=http_get_simple(servald_server,
				    credential,path,f,5000,NULL,0);
    fclose(f);
    if(result_code!=200) {
      fprintf(stderr,"http request failed (%d). URLPATH:%s\n",result_code,path);
      return -1;
    }
    long long t2=gettime_ms();
    f=fopen(filename,"r");
    if (!f) {
      fprintf(stderr,"ERROR: Could not open '%s' to read bundle manifest in prime_bundle_cache() call for bundle #%d\n",
	      filename,bundle_number);
      perror("fopen");
      return -1;
    }
    if (cached_manifest) free(cached_manifest);
    cached_manifest=malloc(8192);
    assert(cached_manifest);
    cached_manifest_len=fread(cached_manifest,1,8192,f);
    cached_manifest=realloc(cached_manifest,cached_manifest_len);
    assert(cached_manifest);
    fclose(f);
    unlink(filename);
    if (0) fprintf(stderr,"  manifest is %d bytes long.\n",cached_manifest_len);

    // Reject over-length manifests
    if (cached_manifest_len>1024) return -1;
    
    // Generate binary encoded manifest from plain text version
    if (cached_manifest_encoded) free(cached_manifest_encoded);
    cached_manifest_encoded=malloc(1024);
    assert(cached_manifest_encoded);
    cached_manifest_encoded_len=0;
    if (manifest_text_to_binary(cached_manifest,cached_manifest_len,
				cached_manifest_encoded,
				&cached_manifest_encoded_len)) {
      // Failed to binary encode manifest, so just copy it
      bcopy(cached_manifest,cached_manifest_encoded,cached_manifest_len);
      cached_manifest_encoded_len = cached_manifest_len;	
    }        
    
    snprintf(path,8192,"/restful/rhizome/%s/raw.bin",
	     bundles[bundle_number].bid_hex);
    snprintf(filename,1024,"%d.%s.raw",getpid(),sid_prefix_hex);
    unlink(filename);
    f=fopen(filename,"w");
    if (!f) {
      fprintf(stderr,"could not open output file '%s'.\n",filename);
      perror("fopen");
      return -1;
    }
    result_code=http_get_simple(servald_server,
				credential,path,f,5000,NULL,0);
    fclose(f); f=NULL;
    if(result_code!=200) {
      fprintf(stderr,"http request failed (%d). URLPATH:%s\n",result_code,path);
      return -1;
    }
    long long t3=gettime_ms();

    if (0)
      fprintf(stderr,"  HTTP pre-fetching of next bundle to send took %lldms + %lldms\n",
	      t2-t1,t3-t2);
    
    // XXX - This transport only allows bundles upto 5MB!
    // (and that is probably pushing it a bit for a mesh extender with only 32MB RAM
    // for everything!)
    f=fopen(filename,"r");
    if (!f) {
      fprintf(stderr,"could read file '%s'.\n",filename);
      perror("fopen");
      return -1;
    }
    if (cached_body) free(cached_body);
    cached_body=malloc(5*1024*1024);
    assert(cached_body);
    // XXX - Should check that we read all the bytes
    cached_body_len=fread(cached_body,1,5*1024*1024,f);    
    cached_body=realloc(cached_body,cached_body_len);
    assert(cached_body);
    fclose(f);
    unlink(filename);
    if (1)
      fprintf(stderr,"  body is %d bytes long. result_code=%d\n",
	      cached_body_len,result_code);

    bid_of_cached_bundle=strdup(bundles[bundle_number].bid_hex);

    cached_version=bundles[bundle_number].version;

    if (0)
      fprintf(stderr,"Cached manifest and body for %s\n",
	      bundles[bundle_number].bid_hex);
  }
  
  return 0;
}
