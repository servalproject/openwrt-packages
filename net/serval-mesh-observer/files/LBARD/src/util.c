
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <termios.h>

#include "sync.h"
#include "lbard.h"
#include "radios.h"
#include "code_instrumentation.h"

#ifdef WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>   // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

// 10K. Might need to be larger. Only causes warnings in sanity checks
#define SENSIBLE_MEMORY_BLOCK_SIZE 10240 

void sleep_ms(int milliseconds) // cross-platform sleep function
{
  LOG_ENTRY;

#ifdef WIN32
  Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
#else
  usleep(milliseconds * 1000);
#endif

  LOG_EXIT;
}


// From os.c in serval-dna
long long gettime_us()
{
  long long retVal = -1;

  LOG_ENTRY;

  do 
  {
    struct timeval nowtv;

    // If gettimeofday() fails or returns an invalid value, all else is lost!
    if (gettimeofday(&nowtv, NULL) == -1)
    {
      LOG_ERROR("gettimeofday returned -1");
      break;
    }

    if (nowtv.tv_sec < 0 || nowtv.tv_usec < 0 || nowtv.tv_usec >= 1000000)
    {
      LOG_ERROR("gettimeofday returned invalid value");
      break;
    }

    retVal = nowtv.tv_sec * 1000000LL + nowtv.tv_usec;
  }
  while (0);

  LOG_EXIT;

  return retVal;
}

// From os.c in serval-dna
long long gettime_ms()
{
  long long retVal = -1;

  LOG_ENTRY;

  do
  {
    struct timeval nowtv;

    // If gettimeofday() fails or returns an invalid value, all else is lost!
    if (gettimeofday(&nowtv, NULL) == -1)
    {
      LOG_ERROR("gettimeofday returned -1");
      break;
    }

    if (nowtv.tv_sec < 0 || nowtv.tv_usec < 0 || nowtv.tv_usec >= 1000000)
    {
      LOG_ERROR("gettimeofday returned invalid value");
      break;      
    }

    retVal = nowtv.tv_sec * 1000LL + nowtv.tv_usec / 1000;
  }
  while (0);

  LOG_EXIT;

  return retVal;
}

int chartohex(int c)
{
  int retVal = -1;

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_ENTRY;
#endif  

  do
  {
    if ((c >= '0') && (c <= '9')) 
    {
      retVal = c-'0';
      break;
    }

    if ((c >= 'A') && (c <= 'F')) 
    {
      retVal = c - 'A' + 10;
      break;
    }

    LOG_ERROR("c is out of range: %d", c);

  }
  while (0);

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_EXIT;
#endif  

  return retVal;
}

int hextochar(int h)
{
  int retVal = '?';

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_ENTRY;
#endif  

  do 
  {
    if ((h >= 0) && (h < 10)) 
    {
      retVal = h + '0';
      break;
    }

    if ((h >= 10) && (h < 16)) 
    {
      retVal = h+'A'-10;
      break;
    }

    LOG_ERROR("h is out of range: %d", h);
  }
  while (0);

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_EXIT;
#endif  

  return retVal;
}

int nybltohexchar(int v)
{
  int retVal = -1;

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_ENTRY;
#endif  

  do 
  {
    if (v < 0)
    { 
      LOG_ERROR("v is out of range: %d", v);
      break;      
    }

    if (v < 10) 
    {
      retVal = '0'+v;
      break;
    }

    if (v < 16)
    {
      retVal = 'A'+v-10;
      break;
    }

    LOG_ERROR("v is out of range: %d", v);
  }
  while (0);

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_EXIT;
#endif  

  return retVal;
}

int ishex(int c)
{
  int retVal = 0;

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_ENTRY;
#endif

  do 
  {
    if (c >= '0' && c <= '9') 
    {
      retVal = 1;
      break;
    }
    if (c >= 'A' && c <= 'F') 
    {
      retVal = 1;
      break;
    }
    if (c >= 'a' && c<= 'f') 
    {
      retVal = 1;
      break;
    }
  }
  while (0);

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_EXIT;
#endif

  return retVal;
}

int chartohexnybl(int c)
{
  int retVal = -1;

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_ENTRY;
#endif  
  do 
  {
    if (c >= '0' && c <= '9') 
    {
      retVal = c - '0';
      break;
    }

    if (c >= 'A' && c <= 'F') 
    {
      retVal = c - 'A' + 10;
      break;
    }

    if (c >= 'a' && c <= 'f') 
    {
      retVal = c - 'a' + 10;
      break;
    }

    LOG_ERROR("c is out of range: %d", c);

  }
  while (0);

#if COMPILE_TEST_LEVEL >= TEST_LEVEL_HEAVY
  LOG_EXIT;
#endif

  return retVal;
}


int hex_encode(unsigned char *in, char *out, int in_len, int radio_type)
{
  int retVal = -1;

  LOG_ENTRY;

  do
  {    
#if COMPILE_TEST_LEVEL >= TEST_LEVEL_LIGHT
    if (! in) 
    {
      LOG_ERROR("in is null");
      break;
    }
    if (! out) 
    {
      LOG_ERROR("out is null");
      break;
    }
    if (in_len > SENSIBLE_MEMORY_BLOCK_SIZE)
    {
      LOG_WARN("out_len seems a bit large: %d", in_len);
    }
    if (radio_type < RADIOTYPE_MIN || radio_type > RADIOTYPE_MAX)
    {
      LOG_WARN("radio_type out of range %d", radio_type);
    }
#endif

    int out_ofs=0;
    int i;

    for (i = 0; i < in_len; i++) {

      out[out_ofs++] = nybltohexchar(in[i]>>4);
      out[out_ofs++] = nybltohexchar(in[i]&0xf);

    }

    out[out_ofs]=0;

    retVal = out_ofs;

  }
  while (0);

  LOG_EXIT;

  return retVal;
}

int hex_decode(char *in, unsigned char *out, int out_len, int radio_type)
{
  int retVal = -1;

  LOG_ENTRY;

  do 
  {
#if COMPILE_TEST_LEVEL >= TEST_LEVEL_LIGHT
    if (! in) 
    {
      LOG_ERROR("in is null");
      break;
    }
    if (! out) 
    {
      LOG_ERROR("out is null");
      break;
    }
    if (out_len > SENSIBLE_MEMORY_BLOCK_SIZE)
    {
      LOG_WARN("out_len seems a bit large: %d", out_len);
    }
    if (radio_type < RADIOTYPE_MIN || radio_type > RADIOTYPE_MAX)
    {
      LOG_WARN("radio_type out of range %d", radio_type);
    }
#endif

    int i;
    int out_count=0;

    int inLen = strlen(in);

    for (i = 0; i < inLen; i+=2) {
      int v = hextochar(in[i+0]) << 4;
      v |= hextochar(in[i+1]);
      out[out_count++] = v;
      if (out_count >= out_len) 
      {
       LOG_ERROR("trying to write more than out_len chars");
       break;//for
      }
    }
    out[out_count] = 0;
    retVal = out_count;
  }
  while (0);

  LOG_EXIT;

  return retVal;
}

int ascii64_encode(unsigned char *in, char *out, int in_len, int radio_type)
{
  int retVal = -1;

  LOG_ENTRY;

  // ASCII-64 is use by HF ALE radio links. It is just ASCII codes 0x20 - 0x5f
  // On Barrett, nothing is escaped.
  // On Codan, spaces must be escaped

  do 
  {
#if COMPILE_TEST_LEVEL >= TEST_LEVEL_LIGHT
    if (! in) 
    {
      LOG_ERROR("in is null");
      break;
    }
    if (! out) 
    {
      LOG_ERROR("out is null");
      break;
    }
    if (in_len > SENSIBLE_MEMORY_BLOCK_SIZE)
    {
      LOG_WARN("in_len seems a bit large: %d", in_len);
    }
    if (radio_type < RADIOTYPE_MIN || radio_type > RADIOTYPE_MAX)
    {
      LOG_WARN("radio_type out of range %d", radio_type);
    }
#endif

    int out_ofs=0;
    int i,j;
    for(i = 0; i < in_len; i+=3) {

      // Encode 3 bytes using 4
      unsigned char ob[4];
      ob[0] = 0x20 +  (in[i+0] & 0x3f);
      ob[1] = 0x20 + ((in[i+0] & 0xc0) >> 6) + ((in[i+1] & 0x0f) << 2);
      ob[2] = 0x20 + ((in[i+1] & 0xf0) >> 4) + ((in[i+2] & 0x03) << 4);
      ob[3] = 0x20 + ((in[i+2] & 0xfc) >> 2);

      // XXX - Character escaping policy should be set in radio driver
      // not in here!
      for(j = 0; j < 4; j++) 
      {
        if ((ob[j] == ' ') && (radio_type == RADIOTYPE_HFCODAN)) 
        {
  	      out[out_ofs++] = '\\';
        }
        out[out_ofs++] = ob[j];
      }
    }

    out[out_ofs]=0;

    retVal = 0;

  }
  while (0);

  LOG_EXIT;

  return retVal;
}

int ascii64_decode(char *in, unsigned char *out, int out_len, int radio_type)
{
  int retVal = -1;

  LOG_ENTRY;

  do
  {
#if COMPILE_TEST_LEVEL >= TEST_LEVEL_LIGHT
    if (! in) 
    {
      LOG_ERROR("in is null");
      break;
    }
    if (! out) 
    {
      LOG_ERROR("out is null");
      break;
    }
    if (out_len > SENSIBLE_MEMORY_BLOCK_SIZE)
    {
      LOG_WARN("out_len seems a bit large: %d", out_len);
    }
    if (radio_type < RADIOTYPE_MIN || radio_type > RADIOTYPE_MAX)
    {
      LOG_WARN("radio_type out of range %d", radio_type);
    }
#endif

    int out_ofs = 0;

    // Makes sure to check for premature string ends (e.g. garbled input data)

    while (*in && out_ofs+3 < out_len) {

      *out = (*in - 0x20) & 0x3f;
      in++;

      if (! *in) 
      {
        LOG_ERROR("premature string end");
        break; //while
      }

      *out |= (((*in - 0x20) & 0x03) << 6);
      in++;
      out++;
      out_ofs++;

      if (! *in) 
      {
        LOG_ERROR("premature string end");
        break; //while
      }

      *out = (((*in - 0x20) & 0x3c) >> 2);
      in++;

      if (! *in) 
      {
        LOG_ERROR("premature string end");
        break; //while
      }

      *out |= (((*in - 0x20) & 0x0f) << 4);
      in++;
      out++;
      out_ofs++;

      if (! *in) 
      {
        LOG_ERROR("premature string end");
        break; //while
      }

      *out = (((*in - 0x20) & 0x30) >> 4);
      in++;

      if (! *in) 
      {
        LOG_ERROR("premature string end");
        break; //while
      }

      *out |= (((*in - 0x20) & 0x3f) << 2);
      in++;
      out++;
      out_ofs++;

    }

    retVal = out_ofs;

  }
  while (0);

  LOG_EXIT;
  
  return retVal;  
}

int dump_bytes(FILE *f,char *msg, unsigned char *bytes, int length)
{
  int retVal = -1;

  LOG_ENTRY;

  do 
  {
#if COMPILE_TEST_LEVEL >= TEST_LEVEL_LIGHT
    if (! msg) 
    {
      LOG_ERROR("msg is null");
      break;
    }
    if (! bytes) 
    {
      LOG_ERROR("bytes is null");
      break;
    }
#endif
    fprintf(f, "%s:\n", msg);
    for (int i = 0; i < length; i += 16)
    {
      fprintf(f, "%04X: ", i);
      for (int j = 0; j < 16; j++)
        if (i + j < length)
          fprintf(f, " %02X", bytes[i + j]);
      fprintf(f, "  ");
      for (int j = 0; j < 16; j++)
      {
        int c;
        if (i + j < length)
          c = bytes[i + j];
        else
          c = ' ';
        if (c < ' ')
          c = '.';
        if (c > 0x7d)
          c = '.';
        fprintf(f, "%c", c);
      }
      fprintf(f, "\n");
    }
    retVal = 0;
  }
  while (0);

  LOG_EXIT;

  return retVal;
}

/* Display a timestamp and current SID.
   This is used so that logs from multiple LBARD instances can be viewed together
   in the output logs of the automated tests, to help identify protocol problems,
   by making visible the sequence of events in chronological order.
   @author PGS 20180329
*/
char timestamp_str_out[1024];
char *timestamp_str(void)
{

  LOG_ENTRY;

  struct tm tm;
  time_t now=time(0);
  struct timeval tv;
  gettimeofday(&tv, NULL);
  localtime_r(&now,&tm);
  snprintf(timestamp_str_out,1024,"[%02d:%02d.%02d.%03d %c%c%c%c*]",
          tm.tm_hour,tm.tm_min,tm.tm_sec,(int)tv.tv_usec/1000,
          my_sid_hex[0],my_sid_hex[1],my_sid_hex[2],my_sid_hex[3]);

  LOG_EXIT;

  return timestamp_str_out;
}

