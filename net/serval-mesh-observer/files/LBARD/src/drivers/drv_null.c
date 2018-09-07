/*

RADIO TYPE: NORADIO,"noradio","No radio",null_radio_detect,null_serviceloop,null_receive_bytes,null_send_packet,null_check_if_ready,10
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
#include <ctype.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "sync.h"
#include "lbard.h"
#include "hf.h"
#include "radios.h"
#include "code_instrumentation.h"

// Import serial_port string from main.c
extern char *serial_port;

int null_radio_detect(int fd)
{
  if ((fd==-1)&&(!strcmp(serial_port,"noradio"))) {
    LOG_NOTE("No serial port, so no radio");
    radio_set_type(RADIOTYPE_NORADIO);
    return 1;
  }
  else return 0;
}

int null_serviceloop(int serialfd)
{
  return 0;
}

int null_receive_bytes(unsigned char *bytes,int count)
{ 
  return 0;
}

int null_send_packet(int serialfd,unsigned char *out, int len)
{
  return 0;
}

int null_check_if_ready(void)
{
  return -1;
}
