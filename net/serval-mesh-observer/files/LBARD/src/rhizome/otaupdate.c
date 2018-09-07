/*
Serval Low-bandwidth asychronous Rhizome Demonstrator.
Copyright (C) 2015-2017 Serval Project Inc.

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

int process_ota_bundle(char *bid,char *version)
{
  char path[8192];
  char filename[1024];
  char versionfile[1024];
  char otaname[1024];
  FILE *f;

  snprintf(path,8192,"/restful/rhizome/%s/raw.bin",
	     bid);
  snprintf(filename,1024,"%s/otaupdate.bin.download",otadir?otadir:"/serval");
  unlink(filename);
  f=fopen(filename,"w");
  if (!f) {
    fprintf(stderr,"could not open output file '%s'.\n",filename);
    perror("fopen");
    return -1;
  }
  int result_code=http_get_simple(servald_server,
				  credential,path,f,5000,NULL,0);
  fclose(f); f=NULL;
  if(result_code!=200) {
    fprintf(stderr,"http request failed (%d). URLPATH:%s\n",result_code,path);
    unlink(filename);
    return -1;
  }

  // Rename into place (and update version file)
  snprintf(otaname,1024,"%s/otaupdate.bin",otadir?otadir:"/serval");
  snprintf(versionfile,1024,"%s/otaupdate.version",otadir?otadir:"/serval");
  unlink(versionfile);
  unlink(otaname);
  rename(filename,otaname);
  f=fopen(versionfile,"w");
  if (f) {
    fprintf(f,"%s\n",version);
    fclose(f);
    return 0;
  }
  
  return -1;
}
