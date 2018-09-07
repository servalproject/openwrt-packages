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

#include "sync.h"
#include "lbard.h"
#include "serial.h"
#include "version.h"
#include "radio_type.h"


#define TMPDIR "/tmp"

int mesh_extender_sad=0;

struct b {
  int order;
  long long priority;
};

char *msgs[1024];
long long msg_times[1024];
int msg_count=0;

int status_log(char *msg)
{
  if (msg_count<1024) {
    msgs[msg_count]=strdup(msg);
    msg_times[msg_count++]=gettime_ms();
    return 0;
  }
  return -1;
}

int update_mesh_extender_health(FILE *f);

// The main job of this routine is to make sure that a character is safe for
// including in HTML output.  So mostly no < or >, but we play it safe.
int safechar(int c)
{
  if (c>='a'&&c<='z') return 1;
  if (c>='A'&&c<='Z') return 1;
  if (c>='0'&&c<='9') return 1;
  switch (c) {
  case '.': case ',': case '-': case '_': case '?':
  case '!': case '@': case '#': case '$': case '%':
  case '^': case '*': case '+': case '=': case '/':
    return 1;
  }

  // Unsafe character
  return 0;
}

int compare_b(const void *a,const void *b)
{
  const struct b *aa=a;
  const struct b *bb=b;

  if (aa->priority<bb->priority) return -1;
  if (aa->priority>bb->priority) return 1;
  return 0;
}

long long status_dump_epoch=0;
time_t last_peer_log=0;

int describe_bundle(int fn, FILE *f,FILE *bundlelogfile,int bn,int peerid,
		    int manifest_offset,int body_offset)
{
  char bid[10];
  char from[128];
  char to[128];
  char fromname[128];
  char toname[128];
  strncpy(from,bundles[bn].sender,127);
  strncpy(to,bundles[bn].recipient,127);

  if (!from[0]) strcpy(from,"unknown");
  if (!to[0]) strcpy(to,"unknown");

  // Try to resolve SIDs to names
  strncpy(fromname,find_sender_name(from),128);
  strncpy(toname,find_sender_name(to),128);
  fromname[32]=0; toname[32]=0;
  
  // Clip sender and recipient SIDs to 8 chars (32 bits)
  from[8]='*';
  to[8]='*';
  from[9]=0;
  to[9]=0;
  
  // Check for invalid characters in to/from
  for(int i=0;i<strlen(from);i++) {
    if (!safechar(from[i])) { strcpy(from,"CENSORED"); break; }
  }
  for(int i=0;i<strlen(to);i++) {
    if (!safechar(to[i])) { strcpy(to,"CENSORED"); break; }
  }
  
  if (!strncasecmp(bundles[bn].service,"MeshMS",6)) {
    // We show both from and to fields
  } else if (!strncasecmp(bundles[bn].service,"MeshMB",6)) {
    // Recipient is "public"
    strcpy(to,"public");
  } else {
    // All others have no sender or recipient
    from[0]=0;
    to[0]=0;
  }
  int j;
  for(j=0;j<8;j++) bid[j]=bundles[bn].bid_hex[j];
  bid[8]='*'; bid[9]=0;
  {
    fprintf(f,"%s/%lld ",
	    bid,bundles[bn].version);
    if (from[0]&&to[0]&&(fn&RESOLVE_SIDS)) {
      if (fromname[0]||toname[0]) {
	fprintf(f,"(%s '%s' -> '%s')",bundles[bn].service,fromname[0]?fromname:from,toname[0]?toname:to);
	fprintf(f,"<br>(%s -> %s)",from,to);
      } else 
	fprintf(f,"(%s %s -> %s)",bundles[bn].service,from,to);
    }
    if (manifest_offset>=0)
      fprintf(f," (from M=%d/P=%d)",manifest_offset,body_offset);
  }

  const time_t now=gettime_ms();
  
  if (manifest_offset>=0)
    if (bundlelogfile&&(fn==0))
      if (peerid>-1)
	fprintf(bundlelogfile,"%lld:T+%lldms:PEERXFER:%s*:%s/%lld (from M=%d/P=%d):%s",
		(long long)now,
		(long long)(gettime_ms()-start_time),		  
		peer_records[peerid]->sid_prefix,
		bid,bundles[bn].version,
		manifest_offset,body_offset,
		ctime(&now));

  return 0;
}


char *home_page="\n"
"  <html>\n"
"  <head>\n"
"    <title>Mesh Extender Packet Radio Link Status</title>\n"
"    <style>\n"
"    h2.section { background-color: powderblue; margin-bottom: 0px;}\n"
"    p { margin-top: 0px; }\n"
"    \n"
"    \n"
"    </style>\n"
"    <script src=\"/js/Chart.min.js\"></script>\n"
"    <script>\n"
"      // Update Time since page load\n"
"      var seconds_since_load = 0;\n"
"      var seconds_since_update = 0;\n"
"      setInterval(function() {\n"
"      seconds_since_load++; seconds_since_update++;\n"
"      document.getElementById('time_since_load').innerHTML = seconds_since_load;\n"
"      document.getElementById('time_since_update').innerHTML = seconds_since_update;\n"
"      }, 1000);\n"
"\n"
"      \n"
"      function toggleElement(e) {\n"
"      var x = document.getElementById(e);\n"
"      if (x.style.display === \"none\") {\n"
"      // Make visible again, and immediately request fresh data.\n"
"      x.busy=false; // clear any pending request for updated content\n"
"      x.style.backgroundColor='#efefef';\n"
"      x.innerHTML=\"Loading...\";\n"
"      x.style.display=\"block\";\n"
"      refreshDiv(e); // then trigger a refresh\n"
"      } else x.style.display=\"none\";\n"
"      }\n"
"\n"
"      function refreshDiv(d) {\n"
"      var x = document.getElementById(d);\n"
"      if (x.style.display === \"none\") return;\n"
"      if (x.busy === true) return;\n"
"      // x.innerHTML=\n"
"      \n"
"      var r = new XMLHttpRequest();\n"
"      r.onreadystatechange=function() {\n"
"      if (r.readyState==4 && r.status==200) {\n"
"      x.innerHTML = r.responseText;\n"
"      x.busy=false;\n"
"      x.style.backgroundColor='#efefef';\n"					   
"      // Reset indication of when we last received information from LBARD / servald\n"
"      seconds_since_update=0;\n"
"      }\n"
"      }\n"
"      x.busy=true;\n"
"      x.style.backgroundColor='#cfcfcf';\n"					   
"      r.open(\"GET\", \"/status/\"+d, true);\n"
"      r.send();\n"
"      \n"
"      }\n"
"      \n"
"      var iterations=0;\n"
"      function periodicRefresh() {\n"
"\n"
"        // Update the contents of any visible information section\n"
"        refreshDiv('meinfo');\n"
"        refreshDiv('radiolinks');\n"
"        refreshDiv('servaldinfo');\n"
"        refreshDiv('txqueue');\n"
"        refreshDiv('bundlerx');\n"
"        // Bundle list can take a long time, so refresh less frequently\n"
"        iterations++;\n"
"        if (iterations>5) {\n"
"        refreshDiv('bundlelist'); iterations=0;\n"
"        }\n"
"        refreshDiv('radioinfo');\n"
"        refreshDiv('diags');\n"
"      }\n"
"\n"
"      var periodicTimer = setInterval(\"periodicRefresh()\",2000);\n"
"      document.addEventListener('DOMContentLoaded', function() {\n"
"          periodicRefresh();\n"
"      }, false);\n"
"      \n"
"    </script>\n"
"  </head>\n"
"  <body>\n"
"    <h1>Serval Mesh Extender Status<br>SID: %s*</h1>\n"
"\n"
"    Page first loaded <span id=time_since_load>0</span> seconds ago.\n"
"    <br>\n"
"    Last update from Mesh Extender received <span id=time_since_update>0</span> seconds ago.\n"
"    <p>\n"
"      \n"
"    <div style='display:none'>\n"
"      <button onClick=\"toggleElement('meinfo')\">Mesh Extender Info</button>\n"
"      <button onClick=\"toggleElement('radiolinks')\">Radio Links</button>\n"
"      <button onClick=\"toggleElement('servaldinfo')\">Wi-Fi Links</button>\n"
"      <button onClick=\"toggleElement('txqueue')\">Transmit Queues</button>\n"
"      <button onClick=\"toggleElement('bundlerx')\">Receive Progress</button>\n"
"      <button onClick=\"toggleElement('bundlelist')\">Stored Bundles</button>\n"
"      <button onClick=\"toggleElement('radioinfo')\">Radio Configuration</button>\n"
"      <button onClick=\"toggleElement('diags')\">Diagnostic Information</button>\n"
"    </div>\n"
"    \n"
"    <h2 class=section><a href=\"javascript:toggleElement('meinfo');\">Mesh Extender Information</a></h2>\n"
"    <div style=\"display:block\" id=meinfo>Loading...</div>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('radiolinks');\">Mesh Extenders Reachable by Radio</a></h2>\n"
"    <div style=\"display:block\" id=radiolinks>Loading...</div>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('servaldinfo');\">Serval DNA status (shows Wi-Fi links)</a></h2>\n"
"    <span style=\"display:none\" id=servaldinfo>Loading...</span>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('txqueue');\">Bundle Transmit Queue</a></h2>\n"
"    <div style=\"display:none\" id=txqueue>Loading...</div>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('bundlerx');\">Bundle Receive Progress</a></h2>\n"
"    <div style=\"display:none\" id=bundlerx>Loading...</div>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('bundlelist');\">Received Bundle List</a></h2>\n"
"    <div style=\"display:none\" id=bundlelist>Loading...</div>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('radioinfo');\">Radio Configuration Information</a></h2>\n"
"    <div style=\"display:none\" id=radioinfo>Loading...</div>\n"
"    <h2 class=section><a href=\"javascript:toggleElement('diags');\">Mesh Extender Diagnostics</a></h2>\n"
"    <div style=\"display:none\" id=diags>Loading...</div>\n"
"  </body>\n"
"</html>\n"
;

int send_status_home_page(int socket)
{
  // Get SID prefix
  char my_sid_hex_prefix[17];
  for(int i=0;i<16;i++) my_sid_hex_prefix[i]=my_sid_hex[i];
  my_sid_hex_prefix[16]=0;

  // Render into main status page
  char home_page_data[strlen(home_page)+100];
  snprintf(home_page_data,strlen(home_page)+99,home_page,my_sid_hex_prefix);

  // Now prepare HTTP response header
  char header[1024];
  snprintf(header,1024,
	   "HTTP/1.0 200 OK\n"
	   "Server: Serval LBARD\n"
	   "Content-length: %d\n"
	   "\n",(int)strlen(home_page_data));

  // Now send it all
  write_all(socket,header,strlen(header));
  write_all(socket,home_page_data,strlen(home_page_data));
  
  return 0;
}

int status_dump()
{
  int i;
  
  if (last_peer_log>time(0)) last_peer_log=time(0);
  
  // Periodically record list of peers in bundle log, if we are maintaining one
  FILE *bundlelogfile=NULL;
  if (debug_bundlelog) {
    if ((time(0)-last_peer_log)>=300) {
      last_peer_log=time(0);	
      bundlelogfile=fopen(bundlelog_filename,"a");
      if (bundlelogfile) {
	fprintf(bundlelogfile,"%lld:T+%lldms:PEERREPORT:%s",
		(long long)last_peer_log,
		(long long)(gettime_ms()-start_time),ctime(&last_peer_log));
      } else perror("Could not open bundle log file");
    }
  }

  for (i=0;i<peer_count;i++) {
    long long age=(time(0)-peer_records[i]->last_message_time);
    float mean_rssi=-1;
    if (peer_records[i]->rssi_counter) mean_rssi=peer_records[i]->rssi_accumulator*1.0/peer_records[i]->rssi_counter;
    int missed_packets=peer_records[i]->missed_packet_count;
    int received_packets=peer_records[i]->rssi_counter;
    
    if (age<=30) {
      time_t now=time(0);

      if (bundlelogfile) {
	fprintf(bundlelogfile,"%lld:T+%lldms:PEERSTATUS:%s*:%lld:%d/%d:%.0f:%s",
		(long long)now,
		(long long)(gettime_ms()-start_time),		  
		peer_records[i]->sid_prefix,
		age,received_packets,received_packets+missed_packets,mean_rssi,
		ctime(&now));
	fprintf(stderr,"Wrote PEERSTATUS line.\n");
      }
    }
  }
  
  if (bundlelogfile) fclose(bundlelogfile);

  if (status_dump_epoch==0) status_dump_epoch=gettime_ms();  

  return 0;
}
  

int status_dump_meinfo(FILE *f,char *topic)
{
  fprintf(f,"<p>LBARD Version commit:%s branch:%s [MD5: %s] @ %s\n<p>\n",
	  GIT_VERSION_STRING,GIT_BRANCH,VERSION_STRING,BUILD_DATE);    

  update_mesh_extender_health(NULL);
  
  if (mesh_extender_sad) {
    fprintf(f,"<p><span style='background-color: #ff0000'>This Mesh Extender is currently suffering from %d problem(s).<br>See diagnostics section on this page for more details.</span>\n",mesh_extender_sad);
  }
  
  return 0;
}

int status_dump_radioinfo(FILE *f, char *topic)
{
  fprintf(f,"Radio detected as '%s'.\n",radio_types[radio_get_type()].name);
  if (radio_last_heartbeat_time)
    fprintf(f,"Last heartbeat received at T-%lld.\n",radio_last_heartbeat_time);
  if (radio_temperature!=9999)
    fprintf(f," Radio temperature %dC\n",radio_temperature);

  // And EEPROM data (copy from /tmp/eeprom.data)
  char buffer[16384];
  FILE *e=fopen("/tmp/eeprom.data","r");
  if (e) {
    fprintf(f,"<h3>EEPROM Radio information</h3>\n<pre>\n");
    int bytes=fread(buffer,1,16384,e);
    if (bytes>0) fwrite(buffer,bytes,1,f);
    fclose(e);
    fprintf(f,"</pre>\n");
  }

  return 0;
}

int status_dump_radiolinks(FILE *f, char *topic)
{
  int i;
  
  // Show peer reachability with indication of activity
  fprintf(f,"<table border=1 padding=2 spacing=2><tr><th>Mesh Extender ID</th><th>Performance</th><th>Receive Signal Strength (RSSI)</th><th>Sending</th></tr>\n");
  for (i=0;i<peer_count;i++) {
    long long age=(time(0)-peer_records[i]->last_message_time);
    float mean_rssi=-1;
    if (peer_records[i]->rssi_counter) mean_rssi=peer_records[i]->rssi_accumulator*1.0/peer_records[i]->rssi_counter;
    int missed_packets=peer_records[i]->missed_packet_count;
    int received_packets=peer_records[i]->rssi_counter;
    float percent_received=0;
    if (received_packets+missed_packets) {
      percent_received=received_packets*100.0/(received_packets+missed_packets);
    }
    char *colour="#00ff00";
    if (percent_received<10) colour="#ff4f00";
    else if (percent_received<50) colour="#ffff00";
    else if (percent_received<80) colour="#c0c0c0";
    
    if (age<=30) {
      fprintf(f,"<tr><td>%s*</td><td bgcolor=\"%s\">%lld sec, %d/%d received (%2.1f%% loss), mean RSSI = %.0f</td>\n",
	      peer_records[i]->sid_prefix,colour,
	      age,received_packets,received_packets+missed_packets,100-percent_received,mean_rssi);
      fprintf(f,"<td>\n");
      log_rssi_graph(f,peer_records[i]);
      fprintf(f,"</td>\n");
      fprintf(f,"<td>");
      
      if (peer_records[i]->tx_bundle!=-1) {
	describe_bundle(RESOLVE_SIDS,
			f,NULL,peer_records[i]->tx_bundle,i,
			peer_records[i]->tx_bundle_manifest_offset_hard_lower_bound,
			peer_records[i]->tx_bundle_body_offset_hard_lower_bound);
      }
      fprintf(f,"</td></tr>\n");
    }
    
    // Reset packet RX stats for next round
    peer_records[i]->missed_packet_count=0;
    peer_records[i]->rssi_counter=0;
    peer_records[i]->rssi_accumulator=0;
  }
  fprintf(f,"</table>\n");

  return 0;
}

int status_dump_bundlerx(FILE *f,char *topic)
{
  int i;
  
  // Show current transfer progress bars
  fprintf(f,"<pre>\n");
  show_progress(f,1);
  fprintf(f,"</pre>\n");

  fprintf(f,"<h3>Bundles in flight</h3>\n<table border=1 padding=2 spacing=2><tr><th>Bundle prefix</th><th>Bundle version</th><th>Progress<th></tr>\n");
  
  for(i=0;i<MAX_BUNDLES_IN_FLIGHT;i++) {
    if (partials[i].bid_prefix) {
      // Here is a bundle in flight
      char *bid_prefix=partials[i].bid_prefix;
      long long version=partials[i].bundle_version;
      char progress_string[80];
      generate_progress_string(&partials[i],
			       progress_string,sizeof(progress_string));
      fprintf(f,"<tr><td>%s*</td><td>%-18lld</td><td>[%s]</td></tr>\n",
	      bid_prefix,version,
	      progress_string);
    }
  }
  fprintf(f,"</table>\n");
  fflush(f);
  
  fprintf(f,"<h3>Announced material</h3>\n<table border=1 padding=2 spacing=2><tr><th>Time</th><th>Announced content</th></tr>\n");
  long long now=gettime_ms();
  for(i=0;i<msg_count;i++) {
    fprintf(f,"<tr><td>T-%lldms</td><td>%s</td></tr>\n",
	    now-msg_times[i],msgs[i]);
    free(msgs[i]); msgs[i]=NULL;
  }
  msg_count=0;
  fprintf(f,"</table>\n");

  return 0;
}

int status_dump_txqueue(FILE *f, char *topic)
{
  int i;
  fprintf(f,"<table border=1 padding=2 spacing=2><tr><th>Bundle</th></tr>\n");
  for (i=0;i<peer_count;i++) {
    long long age=(time(0)-peer_records[i]->last_message_time);
    
    if (age<=30) {
      fprintf(f,"<tr><td><b>Peer %s*</b></td></tr>\n",peer_records[i]->sid_prefix);
      
      for(int j=0;j<peer_records[i]->tx_queue_len;j++) {
	if (peer_records[i]->tx_bundle!=-1) {
	  fprintf(f,"<tr><td>#%d ",peer_records[i]->tx_queue_bundles[j]);
	  describe_bundle(RESOLVE_SIDS
			  ,f,NULL,peer_records[i]->tx_queue_bundles[j],i,
			  // Don't show transfer progress, just bundle info
			  -1,-1);
	  fprintf(f,"</tr>\n");
	}
      }
    }
  }
  fprintf(f,"</table>\n");

  return 0;
}

int status_dump_servaldinfo(FILE *f,char *topic)
{
  long long last_read_time=0;

  fprintf(f,"<table border=1><tr><td>\n");
  int result=http_get_simple(servald_server,NULL,"/",f,2000,&last_read_time,0);
  if (result==-1) {
    fprintf(f,"<h3>ERROR: Could not connect to Serval DNA</h3>\n");
  } else if (result!=200)
    {
      fprintf(f,"HTTP Fetch error:  HTTP Result Code: %d\n",result);
    }
  fprintf(f,"</td></tr></table>\n");
  return 0;
}

int status_dump_bundlelist(FILE *f,char *topic)
{
  int i,n;
  struct b order[bundle_count];
  for (i=0;i<bundle_count;i++) {
    order[i].order=i;
    order[i].priority=bundles[i].last_priority;
  }
  qsort(order,bundle_count,sizeof(struct b),compare_b);
  
  fprintf(f,"<table border=1 padding=2 spacing=2><tr><th>Bundle #</th><th>Bundle</th><th>Bundle version</th><th>Bundle length</th><th>Priority</th><th># peers without it</th></tr>\n");
  for (n=0;n<bundle_count;n++) {
    i=order[n].order;
    fprintf(f,"<tr><td>#%d</td><td>",i);
    describe_bundle(RESOLVE_SIDS ,f,NULL,i,-1,-1,-1);
    
    fprintf(f,"</td><td>%lld</td><td>%lld</td><td>0x%08llx (%lld)</td><td>%d</td></tr>\n",
	    bundles[i].version,
	    bundles[i].length,
	    bundles[i].last_priority,bundles[i].last_priority,
	    bundles[i].num_peers_that_dont_have_it);
  }
  fprintf(f,"</table>\n");
  fflush(f);

  return 0;
}

long long last_fsfix_attempt=0;

int update_mesh_extender_health(FILE *f)
{
  mesh_extender_sad=0;
  int badfs=0;
  
  // XXX - Display file system RW/RO status
  int servalvar_status=-1;
  int serval_status=-1;
  int dos_status=-1;
  char cmd[1024];
  snprintf(cmd,1024,"mount >%s/mount.log",TMPDIR);
  system(cmd);
  snprintf(cmd,1024,"%s/mount.log",TMPDIR);
  FILE *mf=fopen(cmd,"r");
  if (mf) {
    char line[1024];
    line[0]=0; fgets(line,1024,mf);
    while(line[0]) {
      char dev[1024];
      char path[1024];
      char opts[1024];
      if (sscanf(line,"%[^ ] on %[^ ] type %*[^ ] (%[^)]",dev,path,opts)==3)
	{
	  int rw=0;
	  int ro=0;
	  if (opts[0]=='r'&&opts[1]=='w') rw=1;
	  if (opts[0]=='r'&&opts[1]=='o') ro=1;
	  if (!strcasecmp("/serval-var",path)) {
	    if (rw&&(dev[0]=='/')) servalvar_status=0;
	    if (ro) servalvar_status=1;
	  }
	  if (!strcasecmp("/serval",path)) {
	    if (rw&&(dev[0]=='/')) serval_status=0;
	    if (ro) serval_status=1;
	  }
	  if (!strcasecmp("/dos",path)) {
	    if (rw&&(dev[0]=='/')) dos_status=0;
	    if (ro) dos_status=1;
	  }
	}
      line[0]=0; fgets(line,1024,mf);
    }
    fclose(mf);
  }
  switch (dos_status) {
  case 0: break; // ok
  case 1: // Read only
    if (f) fprintf(f,"<p><span style=\"background-color: #ff0000\">/dos partition mounted read only. Problem with USB or SD card?</span>\n");
    mesh_extender_sad++;
    break;
  case -1: // not mounted
    mesh_extender_sad++;
    if (f) fprintf(f,"<p><span style=\"background-color: #ff0000\">No /dos partition. USB or SD card missing?</span>\n");
    break;
  }
  switch (serval_status) {
  case 0: break; // ok
  case 1: // Read only
    mesh_extender_sad++; badfs++;    if (f) fprintf(f,"<p><span style=\"background-color: #ff0000\">/serval partition mounted read only. Problem with USB or SD card?</span>\n");
    break;
  case -1: // not mounted
    mesh_extender_sad++; badfs++;
    if (f) fprintf(f,"<p><span style=\"background-color: #ff0000\">No /serval partition. USB or SD card missing?</span>\n");
    break;
  }
  switch (servalvar_status) {
  case 0: break; // ok
  case 1: // Read only
    mesh_extender_sad++; badfs++;
    if (f) fprintf(f,"<p><span style=\"background-color: #ff0000\">/serval-var partition mounted read only. Problem with USB or SD card?</span>\n");
    break;
  case -1: // not mounted
    mesh_extender_sad++; badfs++;
    if (f) fprintf(f,"<p><span style=\"background-color: #ff0000\">No /serval-var partition. USB or SD card missing?</span>\n");
    break;
  }
  
  // Display servald dead/alive status
  // (inferred from when we last heard anything from it.
  //  this will therefore have some latency, because we don't need to ask
  //  servald things all the time.)
  if (last_servald_contact>gettime_ms()) last_servald_contact=gettime_ms();
  float since_last=(gettime_ms()-last_servald_contact)/1000.0;
  if (since_last<10) 
    { if (f) fprintf(f,"<p>Last contact with Serval DNA %.1f seconds ago\n",since_last); }
  else {
    mesh_extender_sad++;
    if (f) fprintf(f,"<p><span style='background-color: #ff0000'>Last contact with Serval DNA %.1f seconds ago</span>\n",since_last);
  }

  if (badfs&&fix_badfs) {
    // Make sure clock chenanigens can't cause us trouble
    if (last_fsfix_attempt>gettime_ms()) last_fsfix_attempt=gettime_ms();
    if ((gettime_ms()-last_fsfix_attempt)>60000) last_fsfix_attempt=gettime_ms()-60000;
    // Consider calling script to try to fix sad filesystems.
    if ((gettime_ms()-last_fsfix_attempt)>=60000) {
      // It's been a while since we tried fixing it, so try again.
      system("/etc/serval/fixfs");
      last_fsfix_attempt=gettime_ms();
    }
  }
  
  return 0; 
}

int status_dump_diags(FILE *f,char *topic)
{
  update_mesh_extender_health(f);
  show_time_accounting(f);
      
  return 0;
}

// Keep track of when each topic was last updated, and
// how often we should update them.
struct topic_report {
  char name[16];
  time_t last_time;
  int update_interval;
  int (*func)(FILE *f,char *);
};

struct topic_report topics[]={
  {"meinfo",0,1000,status_dump_meinfo},
  {"radiolinks",0,1000,status_dump_radiolinks},
  {"servaldinfo",0,2000,status_dump_servaldinfo},
  {"txqueue",0,3000,status_dump_txqueue},
  {"bundlerx",0,2000,status_dump_bundlerx},
  {"bundlelist",0,10000,status_dump_bundlelist},
  {"radioinfo",0,5000,status_dump_radioinfo},
  {"diags",0,2000,status_dump_diags},
  {"",-1,-1}
};

int http_report_network_status(int socket,char *topic)
{
  if (socket==-1) return -1;

  //  fprintf(stderr,"Request for status page '%s'\n",topic);
  
  // Get filename we need
  char filename[1024];
  snprintf(filename,1024,"%s/%s",TMPDIR,topic);
  
  // Which topic do we need the status for?
  int t=-1;
  for(t=0;topics[t].name[0];t++)
    if (!strcmp(topic,topics[t].name)) break;
  if (!topics[t].name[0]) {
    // Illegal topic
    char m[1024];
    fprintf(stderr,"404 for unknown status page '%s'\n",topic);
    snprintf(m,1024,"HTTP/1.0 404 File not found\nServer: Serval LBARD\n\nCould not read file '%s'\n",filename);
    write_all(socket,m,strlen(m));
    return -1;
  }

  long long age=-1;
  if (topics[t].last_time) {
    if (topics[t].last_time>gettime_ms()) {
      // Last update was in the future, so assume time has
      // run backwards, and thus we should refresh.
      age=-1;
    } else {
      age=gettime_ms()-topics[t].last_time;
      // If update was more than 1 minute ago, then assume that
      // it is either stale and needs and update, or something
      // funny has happend with time updates, and we should
      // update it anyway.
      if (age>60000) age=-1;
    }
  }
  
  // Make file as needing refreshing if the file doesn't exist.
  FILE *f=fopen(filename,"r");
  if (!f) age=-1; else fclose(f);

  if (age<0||age>=topics[t].update_interval) {
    // Update file
    FILE *f=fopen(filename,"w");
    if (!f) {
      fprintf(stderr,"500 for unknown status page '%s', filename='%s'\n",topic,filename);
      perror("fopen");
      char *m="HTTP/1.0 500 Couldn't create temporary file\nServer: Serval LBARD\n\nCould not create temporariy file";
      write_all(socket,m,strlen(m));
      
      return -1;
    }
    // Call function to get file updated
    //    fprintf(stderr,"Regenerating status page '%s'\n",topic);
    topics[t].func(f,topic);
    fclose(f);
    topics[t].last_time=gettime_ms();
  }

  //  fprintf(stderr,"200 for known status page '%s'\n",topic);
  return http_send_file(socket,filename,"text/html");  
}

time_t last_json_network_status_call=0;
int http_report_network_status_json(int socket)
{
  if (((time(0)-last_json_network_status_call)>1)||
      ((time(0)-last_json_network_status_call)<0))
    {
      last_json_network_status_call=time(0);
      FILE *f=fopen("/tmp/networkstatus.json","w");
      if (!f) {
	char *m="HTTP/1.0 500 Couldn't create temporary file\nServer: Serval LBARD\n\nCould not create temporariy file";
	write_all(socket,m,strlen(m));
	
	return -1;
      }

      // List peers
      fprintf(f,"{\n\"neighbours\": [\n     ");
      
      int i;
      int count=0;
      for (i=0;i<peer_count;i++) {
	long long age=(time(0)-peer_records[i]->last_message_time);
	if (age<20) {
	  if (count) fprintf(f,",");
	  fprintf(f,"{ \"id\": \"%s\", \"time-since-last\": %lld }\n",
		  peer_records[i]->sid_prefix,age);
	  count++;
	}
      }
      fprintf(f,"   ],\n\n\n");
      
      
      // Show current transfer progress bars
      fprintf(f,"\n\"transfers\": [\n     ");
      show_progress_json(f,1);
      fprintf(f,"   ]\n}\n\n");      
      
      fclose(f);
    }
  return http_send_file(socket,"/tmp/networkstatus.json","application/json");
}

