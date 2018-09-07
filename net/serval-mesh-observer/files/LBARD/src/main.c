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
#include "hf.h"
#include "code_instrumentation.h"

extern int serial_errors;

int debug_http = 0;
int debug_radio = 0;
int debug_pieces = 0;
int debug_bitmap = 0;
int debug_ack = 0;
int debug_bundles = 0;
int debug_announce = 0;
int debug_pull = 0;
int debug_insert = 0;
int debug_radio_rx = 0;
int debug_radio_tx = 0;
int debug_gpio = 0;
int debug_message_pieces = 0;
int debug_sync = 0;
int debug_sync_keys = 0;
int debug_noprioritisation = 0;
int debug_bundlelog = 0;
char *bundlelog_filename = NULL;

int peer_keepalive_interval=DEFAULT_PEER_KEEPALIVE_INTERVAL;

int fix_badfs = 0;
int nostun = 0;

long long radio_last_heartbeat_time = 0;
int radio_temperature = 9999;

long long last_servald_contact = 0;

// If either of these is not -1, then we try to set them
// for the attached radio.
int txpower = -1;
int txfreq = -1;

char *otabid = NULL;
char *otadir = NULL;

char *onepeer = NULL;

int radio_silence_count = 0;

int http_server = 1;
int udp_time = 0;
int time_slave = 0;
int time_server = 0;
char *time_broadcast_addrs[] = { DEFAULT_BROADCAST_ADDRESSES, NULL };

int reboot_when_stuck = 0;

unsigned char my_sid[32];
unsigned char my_signingid[32];
char *my_sid_hex = NULL;
char *my_signingid_hex = NULL;
unsigned int my_instance_id;
time_t last_instance_time = 0;

char *servald_server = "";
char *credential = "";
char *prefix = "";

char *token = NULL;

time_t last_summary_time = 0;
time_t last_status_time = 0;

int monitor_mode = 0;

struct sync_state *sync_state = NULL;

int urandombytes(unsigned char *buf, size_t len)
{
  int retVal = -1;

  LOG_ENTRY;

  do
  {
#if COMPILE_TEST_LEVEL >= TEST_LEVEL_LIGHT
    if (! buf) 
    {
      LOG_ERROR("buf is null");
      break;
    }
    if (len > 8)
    {
      LOG_NOTE("len seems a bit large: %d", len);
    }
#endif

    static int urandomfd = -1;

    int tries = 0;

    if (urandomfd == -1) 
    {

      for (tries = 0; tries < 4; ++tries) 
      {
        urandomfd = open("/dev/urandom",O_RDONLY);
        if (urandomfd != -1) 
        {
          break;
        }
        LOG_WARN("failed to open /dev/urandom on try #%d, retrying", tries + 1);
        sleep(1);
      }

      if (urandomfd == -1) 
      {
        LOG_ERROR("failed to open /dev/urandom, stop retrying");
        perror("open(/dev/urandom)");
        break;
      }

    }

    tries = 0;
    while (len > 0) 
    {
      ssize_t i = read(urandomfd, buf, (len < 1048576) ? len : 1048576);
      if (i == -1) 
      {
        if (++tries > 4) 
        {
          LOG_ERROR("failed to read from /dev/urandom, even after retries");
          perror("read(/dev/urandom)");
          if (errno==EBADF) 
          {
            LOG_ERROR("EBADF on /dev/urandom, resetting urandomfd to -1");
            urandomfd=-1;
          }
          break; // while
        }
        else 
        {
          LOG_ERROR("failed to read from /dev/urandom, retry %d, retrying", tries);
        }
      } 
      else 
      {
        tries = 0;
        buf += i;
        len -= i;
      }
    }

    if (len == 0) 
    {
      retVal = 0;
    }
  }
  while (0);

  LOG_EXIT;

  return retVal;
}

long long start_time=0;

void crash_handler(int signal)
{
  fprintf(stderr,"SIGABORT intercepted. Exiting cleanly.\n");
  exit(0);
}

unsigned int option_flags=0;

char *serial_port = "/dev/null";

int main(int argc, char **argv)
{
  int exitVal = 0;

  LOG_ENTRY;

  do 
  {
    start_time = gettime_ms();

    // Ignore broken socket connections
    signal(SIGPIPE, SIG_IGN);

    /* Catch SIGABORT, for compatibility with test framework (expects return code 0
       on SIGSTOP */
    struct sigaction sig;
    
    sig.sa_handler = crash_handler;
    sigemptyset(&sig.sa_mask); // Don't block any signals during handler
    
    sig.sa_flags = SA_NODEFER | SA_RESETHAND; // So the signal handler can kill the process by re-sending the same signal to itself
    sigaction(SIGABRT, &sig, NULL);
    
    sig.sa_handler = crash_handler;
    sigemptyset(&sig.sa_mask); // Don't block any signals during handler

    sig.sa_flags = SA_NODEFER | SA_RESETHAND; // So the signal handler can kill the pro
    sigaction(SIGSTOP, &sig, NULL);

    // Setup random seed, so that multiple LBARD's started at the same time
    // can't easily end up in lock step.
    uint32_t seed;
    // Seed generator
    urandombytes((unsigned char *) &seed,sizeof(uint32_t)); 
    srandom(seed);

    // Then skip 0 - 4095 initial values, so that even identical seeds won't
    // easily cause problems
    urandombytes((unsigned char *) &seed, sizeof(uint32_t)); 
    seed &= 0xfff;

    while (seed--) 
    {
      random();
    }
    
    sync_setup();

    // Generate a unique transient instance ID for ourselves.
    // Must be non-zero, as we use zero as a marker for not having yet heard the
    // instance ID of a peer.
    my_instance_id = 0;
    while (my_instance_id == 0)
    {
      urandombytes((unsigned char *) &my_instance_id, sizeof(unsigned int));
    }
    last_instance_time = time(0);

    // MeshMS operations via HTTP, so that we can avoid direct database modification
    // by scripts on the mesh extender devices, and thus avoid database lock problems.
    if ((argc > 1) && ! strcasecmp(argv[1], "meshms")) 
    {
      LOG_NOTE("found meshms param");
      exitVal = meshms_parse_command(argc,argv);
      break;
    }

    if ((argc > 1) && ! strcasecmp(argv[1], "meshmb")) 
    {
      LOG_NOTE("found meshmb param");
      exitVal = meshmb_parse_command(argc,argv);
      break;
    }

    fprintf(stderr,"Version commit:%s branch:%s [MD5: %s] @ %s\n",
    GIT_VERSION_STRING,GIT_BRANCH,VERSION_STRING,BUILD_DATE);
      
    // For Watcharachai's PhD experiments.  Everyone else can safely ignore this option
    if ((argc == 5) && (! strcasecmp(argv[1], "energysamplemaster"))) 
    {
      LOG_NOTE("found energysamplemaster param");
      exitVal = energy_experiment_master(argv[2], argv[3], argv[4]);
      break;
    }

    if ((argc == 5) && (! strcasecmp(argv[1], "energysample"))) 
    {
      LOG_NOTE("found energysample param");

      char *port = argv[2];
      LOG_NOTE("port = %s", port);

      char *interface = argv[3];
      LOG_NOTE("interface = %s", interface);

      char *broadcast_address = argv[4];
      LOG_NOTE("broadcast_address = %s", broadcast_address);

      exitVal = energy_experiment(port, interface, broadcast_address);
      break;
    }

    if ((argc == 5) && (! strcasecmp(argv[1], "energysamplecalibrate"))) 
    {
      LOG_NOTE("found energysamplecalibrate param");

      char *port=argv[2];
      LOG_NOTE("port = %s", port);

      char *broadcast_address=argv[3];
      LOG_NOTE("broadcast_address = %s", broadcast_address);

      char *exp_string = argv[4];
      LOG_NOTE("exp_string = %s", exp_string);

      exitVal = energy_experiment_calibrate(port, broadcast_address, exp_string);
      break;
    }

    if 
    (
        (argc == 3)
      && 
        (
          (! strcasecmp(argv[1], "monitor"))
        ||
          (! strcasecmp(argv[1], "monitorts"))
        ) 
    )
    {
      LOG_NOTE("found %s param", argv[1]);

      if (! strcasecmp(argv[1], "monitorts")) 
      {
        time_server = 1; 
        udp_time = 1; 
      }

      monitor_mode = 1;
      debug_pieces = 1;
      debug_message_pieces = 1;

      serial_port = argv[2];
      LOG_NOTE("serial_port = %s", serial_port);
    } 
    else 
    {  
      if (argc<5) 
      {
        LOG_NOTE("less than 5 arguments");

        fprintf(stderr,"usage: lbard <servald hostname:port> <servald credential> <my sid> <my signing id> <serial port> [options ...]\n");
        fprintf(stderr,"usage: lbard monitor <serial port>\n");
        fprintf(stderr,"usage: lbard meshms <meshms command>\n");
        fprintf(stderr,"usage: lbard meshmb <meshmb command>\n");
        fprintf(stderr,"usage: energysamplecalibrate <args>\n");
        fprintf(stderr,"usage: energysamplemaster <broadcast addr> <backchannel addr> <gapusec=n,holdusec=n,packetbytes=n>\n");
        fprintf(stderr,"usage: energysample <port> <interface> <broadcast address>\n");
        exitVal = -1;
        break;
      }

      serial_port = argv[5];
      LOG_NOTE("serial_port = %s", serial_port);
    }

    if (message_update_interval < 0) 
    {
      message_update_interval = 0;
    }
    
    last_message_update_time = 0;
    next_message_update_time = 0;
    congestion_update_time = 0;
    
    my_sid_hex = "000000000000000000000000000000000";
    prefix = "000000";
    my_signingid_hex = "0000000000000000000000000000000";
    if (! monitor_mode) 
    {
      if (argc > 3) 
      {
        char hex[3];
        char* pChr;
        hex[2] = 0;

        prefix = strdup(argv[3]);
        if (strlen(prefix) != 64) 
        {
          LOG_ERROR("prefix length is not 64");
          fprintf(stderr,"You must provide a valid SID for the ID of the local node.\n");
          exitVal = -1;
          break;
        }

        prefix[6]=0;  

        // set my_sid from argv[3]
        pChr = argv[3];
        for (int i = 0; i < 32; i++) 
        {
          hex[0] = *(pChr++);
          hex[1] = *(pChr++);
          my_sid[i] = strtoll(hex, NULL, 16);
        }

        my_sid_hex = strdup(argv[3]);

        if (argc > 4)
        {
          if (strlen(argv[4]) != 64) 
          {
            LOG_ERROR("signing ID length is not 64");
            fprintf(stderr,"You must provide a valid signing ID for the ID of the local node.\n");
            exitVal = -1;
            break;
          }

          // set my_signingid from argv[4]
          pChr = argv[4];
          for (int i = 0; i < 32; i++) 
          {
            hex[0] = *(pChr++);
            hex[1] = *(pChr++);
            my_signingid[i] = strtoll(hex, NULL, 16);
          }
          my_signingid_hex = strdup(argv[4]);
        }
      }
    }

    fprintf(stderr, "%d:My SigningID as hex is %s\n", __LINE__, my_signingid_hex);
    fprintf(stderr, "%d:My SID as hex is %s\n", __LINE__, my_sid_hex);
    fprintf(stderr, 
       "My SID prefix is %02X%02X%02X%02X%02X%02X\n",
       my_sid[0],
       my_sid[1],
       my_sid[2],
       my_sid[3],
       my_sid[4],
       my_sid[5]);
    
    if (argc > 2) 
    {
      credential = argv[2];
      LOG_NOTE("credential = %s", credential);
    }

    if (argc > 1) 
    {
      servald_server = argv[1];
      LOG_NOTE("servald_server = %s", servald_server);
    }

    /*
      The serial port is normally exactly that. However,
      for internet-mediated transports, the serial port is
      also allowed to be a network address.  Basically if
      the serial port name contains a :, i.e., looks like a
      URI, then we don't try to open the port.
    */
    int serialfd = -1;
    if (strstr(serial_port,":")) {
      // Has a :, so assume it is a URI kind of thing
      fprintf(stderr,"Serial port looks like a URI, not (yet) opening/connecting\n");
      LOG_NOTE("Serial port looks like a URI, not (yet) opening/connecting\n");
      
      // So skip serial port fiddling, and go direct to auto detection routines
      autodetect_radio_type(serialfd);
    } else if (!strcmp("noradio",serial_port)) {
      // Force detection as no radio
      serialfd=-1;
      null_radio_detect(serialfd);
    } else {
      serialfd = open(serial_port,O_RDWR);
      if (serialfd < 0) {
	LOG_ERROR("cannot open serial port: %d ('%s')", serialfd,serial_port);
	perror("Opening serial port in main");
	exitVal = -1;
	break;
      }

      if (serial_setup_port(serialfd))
	{
	  LOG_ERROR("cannot set up serial port");
	  fprintf(stderr,"Failed to setup serial port. Exiting.\n");
	  exitVal = -1;
	  break;
	}
      
      fprintf(stderr,"Serial port open as fd %d\n",serialfd);
      LOG_NOTE("Serial port open as fd %d",serialfd);
    }
      
    int n = 6;
    while (n < argc) 
    {
      if (argv[n]) 
      {
        if (! strcasecmp("monitor", argv[n])) 
        {
          LOG_NOTE("monitor mode set to 1");
          monitor_mode = 1;
        } 
        else if (! strcasecmp("meshmsonly",argv[n])) 
        {
          LOG_NOTE("meshmsonly");
          meshms_only = 1;
          fprintf(stderr,"Only MeshMS bundles will be carried.\n");
        }
        else if (! strncasecmp("minversion=", argv[n], 11)) 
        {
          LOG_NOTE("minversion specified");
          int day,month,year;
          min_version = strtoll(&argv[n][11],NULL,10)*1000LL;
          if (sscanf(argv[n], "minversion=%d/%d/%d", &year, &month, &day) == 3) 
          {
            // Minimum date has been specified using year/month/day
            // Calculate min_version from that.
            struct tm tm;
            bzero(&tm, sizeof(struct tm));
            tm.tm_mday = day;
            tm.tm_mon = month - 1;
            tm.tm_year = year - 1900;
            time_t thetime = mktime(&tm);
            min_version= ((long long) thetime) * 1000LL;
            LOG_NOTE("minversion set to %lld", (long long) min_version);
          }
          else 
          {
            LOG_NOTE("minversion not in yyyy/mm/dd format");
          }
          time_t mv=(min_version/1000LL);

          // Get minimum time as non NL terminated string
          char stringtime[1024];
          snprintf(stringtime, 1024, "%s", ctime(&mv));
          if (stringtime[strlen(stringtime)-1] == '\n') 
          {
            stringtime[strlen(stringtime)-1] = '\0';
          }

          fprintf(
            stderr,
            "Only bundles newer than epoch+%lld msec (%s) will be carried.\n",
            (long long) min_version,
            stringtime);
        }
        else if (! strcasecmp("rebootwhenstuck", argv[n])) 
        {
          reboot_when_stuck = 1;
          LOG_NOTE("reboot_when_stuck set to 1");
        }
        else if (! strcasecmp("timeslave", argv[n])) 
        {
          time_slave = 1;
          LOG_NOTE("time_slave set to 1");
        }
        else if (! strcasecmp("timemaster", argv[n])) 
        {
          time_server=1;
          LOG_NOTE("time_server set to 1");
        }
        else if (! strncasecmp("timebroadcast=", argv[n], 14)) 
        {
          time_broadcast_addrs[0] = strdup(&argv[n][14]);
          LOG_NOTE("time_broadcast_addrs[0] = %s", time_broadcast_addrs[0]);
        }
        else if (! strcasecmp("logrejects", argv[n])) 
        {
          debug_insert = 1;
          LOG_NOTE("debug_insert set to 1");
        }
        else if (! strcasecmp("pull", argv[n])) 
        {
          debug_pull = 1;
          LOG_NOTE("debug_pull set to 1");
        }
        else if (! strcasecmp("bitmap", argv[n])) 
        {
          debug_bitmap = 1;
          LOG_NOTE("debug_bitmap set to 1");
        }
        else if (!strcasecmp("ack",argv[n])) 
        {
          debug_ack = 1;
          LOG_NOTE("debug_ack set to 1");
        }
        else if (! strcasecmp("radio", argv[n])) 
        {
          debug_radio = 1;
          LOG_NOTE("debug_radio set to 1");
        }
        else if (! strcasecmp("pieces", argv[n])) 
        {
          debug_pieces = 1;
          LOG_NOTE("debug_pieces set to 1");
        }
        else if (! strcasecmp("bundles", argv[n])) 
        {
          debug_bundles = 1;
          LOG_NOTE("debug_bundles set to 1");
        }
        else if (! strcasecmp("http", argv[n])) 
        {
          debug_http = 1;
          LOG_NOTE("debug_http set to 1");
        }
        else if (! strcasecmp("announce", argv[n])) 
        {
          debug_announce = 1;
          LOG_NOTE("debug_announce set to 1");
        }
        else if (! strcasecmp("insert", argv[n])) 
        {
          debug_insert = 1;
          LOG_NOTE("debug_insert set to 1");
        }
        else if (! strcasecmp("radio_rx", argv[n]))
        {
          debug_radio_rx = 1;
          LOG_NOTE("debug_radio_rx set to 1");
        }
        else if (! strcasecmp("radio_tx", argv[n])) 
        {
          debug_radio_tx = 1;
          LOG_NOTE("debug_radio_tx set to 1");
        }
        else if (! strcasecmp("gpio", argv[n])) 
        {
          debug_gpio = 1;
          LOG_NOTE("debug_gpio set to 1");
        }
        else if (! strcasecmp("message_pieces", argv[n])) 
        {
          debug_message_pieces = 1;
          LOG_NOTE("debug_message_pieces set to 1");
        }
        else if (! strcasecmp("sync", argv[n])) 
        {
          debug_sync = 1;
          LOG_NOTE("debug_sync set to 1");
        }
        else if (! strcasecmp("sync_keys", argv[n])) 
        {
          debug_sync_keys = 1;
          LOG_NOTE("debug_sync_keys set to 1");
        }
        else if (! strcasecmp("udptime", argv[n])) 
        {
          udp_time = 1;
          LOG_NOTE("udp_time set to 1");
        }
        else if (!strcasecmp("fixfs",argv[n])) 
        {
          fix_badfs = 1;
          LOG_NOTE("fix_badfs set to 1");
        }
        else if (!strcasecmp("nostun",argv[n])) 
        {
          nostun = 1;
          LOG_NOTE("nostun set to 1");
        }        
        else if (! strncasecmp("outernetrx=", argv[n], 11)) 
        {
          char *outernet_socketname = strdup(&argv[n][11]);
	  if (outernet_rx_setup(outernet_socketname)) {
	    exitVal=-3;
	    break;
	  }
	  LOG_NOTE("Outernet socket name is '%s'",outernet_socketname);
	}
	else if (! strncasecmp("bundlelog=", argv[n], 10)) 
        {
          bundlelog_filename = strdup(&argv[n][10]);
          LOG_NOTE("bundlelog_filename: %s", bundlelog_filename);
          debug_bundlelog = 1;
          LOG_NOTE("debug_bundlelog set to 1");
          fprintf(
            stderr,
            "Will log bundle receipts and peer connectivity to '%s'\n",
            bundlelog_filename);
        } 
        else if (! strcasecmp("nopriority", argv[n])) 
        {
          debug_noprioritisation = 1;
          LOG_NOTE("debug_noprioritisation set to 1");
        }
        else if (! strcasecmp("nohttpd", argv[n])) 
        {
          http_server = 0;
          LOG_NOTE("http_server set to 0");
        }
        else if (! strncasecmp("txpower=", argv[n], 8)) 
        {
          txpower = atoi(&argv[n][8]);
          LOG_NOTE("txpower set to %d", txpower);
          if (txpower < 1 || txpower > 90) 
          {
            LOG_ERROR("txpower out of range");
            fprintf(stderr,"TX power must be between 1 and 90dBm (but not all radios support all power levels)\n");
            exitVal = -1;
            break;
          }
        } 
        else if (! strncasecmp("txfreq=", argv[n], 6)) 
        {
          txfreq = atoi(&argv[n][6]);
          LOG_NOTE("txfreq set to %d", txfreq);
          if (txfreq < 1 || txfreq > 1200000000) 
          {
            LOG_ERROR("txfreq out of range");
            fprintf(stderr,"TX frequency must be between 1Hz and 1.2GHz, expressed as Hz\n");
            exitVal = -1;
            break;
          }
        } 
        else if (! strncasecmp("flags=", argv[n], 6)) 
        {
          option_flags = atoi(&argv[n][6]);
          LOG_NOTE("option_flags set to %d", option_flags);
          fprintf(
            stderr,
            "Option flags = %d (from '%s')\n",
            option_flags,
            &argv[n][6]);
        } 
        else if (! strncasecmp("packetrate=", argv[n], 11))
        {
          target_transmissions_per_4seconds = atoi(&argv[n][11]);
          LOG_NOTE("target_transmissions_per_4seconds set to %d", target_transmissions_per_4seconds);
        }
        else if (! strncasecmp("otabid=", argv[n], 7)) 
        {
          // BID of Over The Air Update Rhizome bundle
          otabid = strdup(&argv[n][7]);
          LOG_NOTE("otabid: %s", otabid);
          fprintf(stderr,"OTA BID is '%s'\n", otabid);
        } 
        else if (! strncasecmp("otadir=", argv[n], 7)) 
        {
          // Where to put OTA update file
          otadir = strdup(&argv[n][7]);
          LOG_NOTE("otadir: %s", otadir);
          fprintf(stderr,"OTA directory is '%s'\n", otadir);
        } 
        else if (! strncasecmp("onepeer=", argv[n], 8)) 
        {
          // SID of the single UHF peer we are allowed to talk to
          // (this is a debug capability)
          onepeer = strdup(&argv[n][8]);
          LOG_NOTE("onepeer: %s", onepeer);
          fprintf(stderr,"Radio peering restricted to %s*\n", onepeer);
        } 
        else if (! strncasecmp("periodicrequests=", argv[n], 17)) 
        {
          // Setup periodic RESTful API requests to create a static
          // proxy of RESTful content requests that can be accessed
          // by web clients of a mesh extender
          LOG_NOTE("periodicrequests: %s", &argv[n][17]);
          fprintf(
            stderr,
            "Periodic RESTful request configuration file is '%s'\n",
            &argv[n][17]);
          setup_periodic_requests(&argv[n][17]);
        } 
        else 
        {
          LOG_ERROR("illegal mode: %s", argv[n]);
          fprintf(stderr,"Illegal mode '%s'\n",argv[n]);
          exitVal = -3;
          break;
        }
      }
      n++;
    }
    if (exitVal)
    {
      break;
    }

    // Open UDP socket to listen for time updates from other LBARD instances
    // (poor man's NTP for LBARD nodes that lack internal clocks)
    int timesocket = -1;
    if (udp_time) 
    {
      timesocket = socket(AF_INET, SOCK_DGRAM, 0);
      if (timesocket == -1) 
      {
        LOG_ERROR("timesocket is -1");
      }
      else
      {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(0x5401);
        bind(timesocket, (struct sockaddr *) &addr, sizeof(addr));
        set_nonblock(timesocket);

        // Enable broadcast
        int one = 1;
        int r = setsockopt(timesocket, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
        if (r) 
        {
          LOG_ERROR("Could not enable SO_BROADCAST");
          fprintf(stderr,"WARNING: setsockopt(): Could not enable SO_BROADCAST\n");
        }

      }
    }

    // HTTP Server socket for accepting MeshMS message submission via web form
    // (Used for sending anonymous messages to a help desk for a mesh network, and
    //  for providing simple web-based diagnostics).
    int httpsocket=-1;
    if (http_server) 
    {
      httpsocket=socket(AF_INET, SOCK_STREAM, 0);
      if (httpsocket == -1) 
      {
        LOG_ERROR("httpsocket is -1");
      }
      else
      {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(0x5402);
        int optval = 1;
        setsockopt(httpsocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        bind(httpsocket, (struct sockaddr *) &addr, sizeof(addr));
        set_nonblock(httpsocket);
        listen(httpsocket, 10);
      }
      
    }

    char token[1024] = "";
    
    while (exitVal == 0) 
    {
      unsigned char msg_out[LINK_MTU];

      account_time("ID regenerate");

      // Refresh our instance ID every four minutes, so that any bundle list sync bugs
      // can only block transmission for a few minutes.
      if ((time(0) - last_instance_time) > 240) 
      {
        my_instance_id = 0;
        while(my_instance_id == 0)
        {
          urandombytes((unsigned char *) &my_instance_id, sizeof(unsigned int));
        }

        last_instance_time = time(0);
      }
      
      account_time("radio_read_bytes()");
      
      radio_read_bytes(serialfd, monitor_mode);

      account_time("outernet_rx_serviceloop()");
      
      outernet_rx_serviceloop();
      
      account_time("load_rhizome_db_async()");

      load_rhizome_db_async(servald_server, credential, token);

      account_time("make_periodic_requests()");

      make_periodic_requests();

      account_time("radio.serviceloop()");

      if (radio_get_type() >= 0) 
      {
        if (! radio_types[radio_get_type()].serviceloop) 
        {
          LOG_ERROR("Illegal radio type");
          fprintf(
            stderr,
            "Radio type set to illegal value %d\n",
            radio_get_type());
          exitVal = -1;
          break;
        }

        radio_types[radio_get_type()].serviceloop(serialfd);
      }
      else 
      {
        LOG_ERROR("Unknown radio type");
        fprintf(stderr,"ERROR: Connected to unknown radio type.\n");
        exitVal = -1;
        break;
      }

      account_time("time server: reverse timeflow check");

      // Deal gracefully with clocks that run backwards from time to time.
      if (last_message_update_time > gettime_ms())
      {
        LOG_WARN("Clock went backwards: clock delta=%lld",last_message_update_time-gettime_ms());
        last_message_update_time = gettime_ms();
      }
      
      account_time("time server: announce ");
      
      if (gettime_ms() >= next_message_update_time)
      {
        if (! time_server) 
        {
          // Decay my time stratum slightly
          if (my_time_stratum < 0xffff)
          {
            my_time_stratum++;
          }
        } 
        else 
        {
          my_time_stratum = 0x0100;
        }

        // Send time packet
        if (udp_time && (timesocket !=- 1)) 
        {
          // Occassionally announce our time
          // T + (our stratum) + (64 bit seconds since 1970) +
          // + (24 bit microseconds)
          // = 1+1+8+3 = 13 bytes
          struct timeval tv;
          gettimeofday(&tv,NULL);    
          
          unsigned char msg_out[1024];
          int offset=0;
          append_timestamp(msg_out,&offset);
          
          // Now broadcast on every interface to port 0x5401
          // Oh that's right, UDP sockets don't have an easy way to do that.
          // We could interrogate the OS to ask about all interfaces, but we
          // can instead get away with having a single simple broadcast address
          // supplied as part of the timeserver command line argument.
          struct sockaddr_in addr;
          bzero(&addr, sizeof(addr)); 
          addr.sin_family = AF_INET; 
          addr.sin_port = htons(0x5401);
          int i;
          for ( i = 0; time_broadcast_addrs[i]; i++) 
          {
            addr.sin_addr.s_addr = inet_addr(time_broadcast_addrs[i]);
            errno=0;
            sendto(
              timesocket,
              msg_out,
              offset,
              MSG_DONTROUTE
              | MSG_DONTWAIT
#ifdef MSG_NOSIGNAL
              | MSG_NOSIGNAL
#endif         
             , (const struct sockaddr *)&addr, 
             sizeof(addr));
          }
          // printf("--- Sent %d time announcement packets.\n",i);

      	  account_time("time server: rx ");

          // Check for time packet
          if (timesocket != -1)
          {
            unsigned char msg[1024];
            int offset = 0;
            int r = recvfrom(timesocket, msg, 1024, 0, NULL, 0);
            if (r == (1+1+8+3)) 
            {
              // see rxmessages.c for more explanation
              offset++;
              int stratum = msg[offset++];
              struct timeval tv;
              bzero(&tv, sizeof(struct timeval));
              for (int i = 0; i < 8; i++) 
              {
                tv.tv_sec|=msg[offset++]<<(i*8);
              }

              for (int i = 0; i < 3; i++) 
              {
                tv.tv_usec|=msg[offset++]<<(i*8);
              }

              // ethernet delay is typically 0.1 - 5ms, so assume 5ms
              tv.tv_usec += 5000;

              saw_timestamp("          UDP", stratum, &tv);
            }
          } 
        }
        if (httpsocket != -1)
        {
          struct sockaddr cliaddr;
          socklen_t addrlen;
     	    account_time("HTTP accept()");
          int s = accept(httpsocket, &cliaddr, &addrlen);
          if (s != -1) 
          {
            // HTTP request socket
            //   printf("HTTP Socket connection\n");
            // Process socket
            // XXX This is synchronous to keep things simple,
            // which is part of why we only check every second or so
            // for one new connection.  We also don't allow the request
            // to linger: if it doesn't contain the request almost immediately,
            // we reject it with a timeout error.
    	      account_time("http_process()");
            http_process(&cliaddr, servald_server, credential, my_sid_hex, s);
          }
        }

        account_time("update_my_message()");
        
        if ((! monitor_mode) && radio_ready()) 
        {
          update_my_message(
            serialfd,
            my_sid,
            my_sid_hex,
            LINK_MTU,
            msg_out,
            servald_server,
            credential);
    
          // Vary next update time by upto 250ms, to prevent radios getting lock-stepped.
          if (message_update_interval_randomness)
          {
            next_message_update_time = gettime_ms() + (random()%message_update_interval_randomness) + message_update_interval;
          }
          else
          {
            next_message_update_time = gettime_ms() + message_update_interval;
          }
        }
    
       	account_time("status_dump()");
    
        // Update the state file to help debug things
        // (but not too often, since it is SLOW on the MR3020s
        //  XXX fix all those linear searches, and it will be fine!)
      	if (last_status_time>time(0)) 
      	{
        	last_status_time=time(0);
        }

        if (time(0) > last_status_time) {
          last_status_time = time(0) + 2;
          status_dump();
        }
        
      	account_time("post_status_dump()");
      	
      }

      account_time("stuck serial reboot check");

      if ((serial_errors>20) && reboot_when_stuck) 
      {
        LOG_ERROR("rebooting");        
        // If we are unable to write to the serial port repeatedly for a while,
        // we could be facing funny serial port behaviour bugs that we see on the MR3020.
        // In which case, if authorised, ask the MR3020 to reboot
        system("reboot");
      }
      
      account_time("stun_serviceloop()");

      if (! nostun) stun_serviceloop();

      account_time("usleep()");
      
      usleep(10000);

      account_time("show_progress()");

      if (time(0) > last_summary_time) 
      {
        last_summary_time = time(0);
        show_progress(stderr, 0);
      }
      
      account_time("End of loop");
      
    }
  }
  while (0);

  LOG_EXIT;

  exit(exitVal);
}
