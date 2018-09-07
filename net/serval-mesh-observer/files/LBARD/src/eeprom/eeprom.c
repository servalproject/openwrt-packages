#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "sha3.h"

// Radio parameters we get from the EEPROM
char regulatory_information[16384]="No regulatory information provided.";
char configuration_directives[16384]="nodirectives=true\n";

// Use public domain libz-compatible library
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
//#define MINIZ_NO_ZLIB_APIS
#include "miniz.c"

int eeprom_write_page(int fd, int address,unsigned char *readblock);

int eeprom_decode_data(char *msg,unsigned char *datablock,FILE *f)
{

  // Parse radio parameter block
  // See http://developer.servalproject.org/dokuwiki/doku.php?id=content:meshextender:me2.0_eeprom_plan&#memory_layout

  sha3_Init256();
  sha3_Update(&datablock[0x7C0],(0x7F0-0x7C0));
  sha3_Finalize();
  int i;
  for(i=0;i<16;i++) if (datablock[0x7F0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(f,"Radio parameter block checksum valid.\n");

    uint8_t format_version=datablock[0x7EF];
    char primary_iso_code[3]={datablock[0x7ED],datablock[0x7EE],0};
    uint8_t regulatory_lock_required=datablock[0x7E8];
    uint16_t radio_bitrate=(datablock[0x7E7]<<8)+(datablock[0x7E6]<<0);
    uint32_t radio_centre_frequency=
      (datablock[0x7E3]<<8)+(datablock[0x7E2]<<0)+
      (datablock[0x7E5]<<24)+(datablock[0x7E4]<<16);
    uint8_t radio_txpower_dbm=datablock[0x7E1];
    uint8_t radio_max_dutycycle=datablock[0x7E0];

    if (format_version!=0x01) {
      fprintf(f,"Radio parameter block data format version is 0x%02x, which I don't understand.\n",format_version);
    } else {    
      fprintf(f,
	      "                radio max TX power = %d dBm\n",
	      (int)radio_txpower_dbm);
      fprintf(f,
	      "              radio max duty-cycle = %d %%\n",
	      (int)radio_max_dutycycle);
      fprintf(f,
	      "                   radio air speed = %d Kbit/sec\n",
	      (int)radio_bitrate);
      fprintf(f,
	      "            radio centre frequency = %d Hz\n",
	      (int)radio_centre_frequency);
      fprintf(f,
	      "regulations require firmware lock? = '%c'\n",
	      regulatory_lock_required);
      fprintf(f,
	      "          primary ISO country code = \"%s\"\n",
	      primary_iso_code);

      // XXX - Store all parameters for reference.  In particular,
      // we care about radio_max_dutycycle and regulatory_lock_required, so that
      // we can obey them.
    }
  }
  else fprintf(f,
	       "ERROR: Radio parameter block checksum is wrong:\n"	       
	       "       Radio will ignore EEPROM data!\n");

  // Parse extended regulatory information (country list etc)
  sha3_Init256();
  sha3_Update(&datablock[0x0400],0x7B0-0x400);
  sha3_Finalize();
  for(i=0;i<16;i++) if (datablock[0x7B0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(f,
	    "Radio regulatory information text checksum is valid.\n");
    unsigned long regulatory_information_length=sizeof(regulatory_information);
    int result=mz_uncompress((unsigned char *)regulatory_information,
			     &regulatory_information_length,
			     &datablock[0x400], 0x7B0-0x400);
    if (result!=MZ_OK)
      fprintf(f,"Failed to decompress regulatory information block.\n");
    else {
      // XXX - This should be recorded somewhere, so that we can present it using
      // our web server.
      fprintf(f,
	      "The information text is as follows:\n  > ");
      for(i=0;regulatory_information[i];i++) {
	if (regulatory_information[i]) fprintf(f,"%c",regulatory_information[i]);
	if ((regulatory_information[i]=='\r')||(regulatory_information[i]=='\n'))
	  fprintf(f,"  > ");
      }
      fprintf(f,"\n");
    }
  } else fprintf(f,
		 "ERROR: Radio regulatory information text checksum is wrong:\n"	       
		 "       LBARD will report only ISO code from radio parameter block.\n");

  // Parse user extended information area
  sha3_Init256();
  sha3_Update(&datablock[0x000],0x3F0);
  sha3_Finalize();
  for(i=0;i<16;i++) if (datablock[0x3E0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(f,
	    "Mesh-Extender configuration directive text checksum is valid.\n"
	    "The information text is as follows:\n  > ");
    unsigned long configuration_directives_length=sizeof(configuration_directives);
    int result=mz_uncompress((unsigned char *)configuration_directives,
			     &configuration_directives_length,
			     &datablock[0x0], 0x3F0);
    if (result!=MZ_OK)
      fprintf(f,"Failed to decompress configuration directive block.\n");
    else {
    for(i=0;configuration_directives[i];i++) {
      if (configuration_directives[i])
	fprintf(f,"%c",configuration_directives[i]);
      if ((configuration_directives[i]=='\r')||(configuration_directives[i]=='\n'))
	fprintf(f,"  > ");
    }
    fprintf(f,"\n");
    }
  } else
    fprintf(f,
	    "ERROR: Mesh-Extender configuration directive block checksum is wrong:\n");
  
  return 0;
}

int eeprom_parse_line(char *line,unsigned char *datablock)
{
  int address;
  int b[16];
  int err;
  if (sscanf(line,"EPR:%x : READ ERROR #%d",&address,&err)==2)
    {
      fprintf(stderr,"EEPROM read error #%d @ 0x%x\n",err,address);
      for(int i=0;i<16;i++) datablock[address+i]=0xee;
    }

  if (sscanf(line,"EPR:%x : %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
	     &address,
	     &b[0],&b[1],&b[2],&b[3],
	     &b[4],&b[5],&b[6],&b[7],
	     &b[8],&b[9],&b[10],&b[11],
	     &b[12],&b[13],&b[14],&b[15])==17) {
    for(int i=0;i<16;i++) datablock[address+i]=b[i];
  }
  
  return 0;
}

char line[1024];
int line_len=0;

int eeprom_parse_output(int fd,unsigned char *datablock)
{
  char buffer[16384];
  int count=read(fd,buffer,16384);
  
  for(int i=0;i<count;i++) {
    if (line_len) {
      if (buffer[i]!='\r')
	{ if (line_len<1000) line[line_len++]=buffer[i]; }
      else {
	line[line_len]=0;
	eeprom_parse_line(line,datablock);
	line_len=0;
      }
    } else {
      if ((buffer[i]=='E')&&(buffer[i+1]=='P')) {
	line[0]='E'; line_len=1;
      }
    }
  }
  if (line_len) eeprom_parse_line(line,datablock);
  
  return 0;
}

int eeprom_read(int fd)
{
  unsigned char readblock[2048];
  char cmd[1024];
  
  fprintf(stderr,"Reading data from EEPROM"); fflush(stderr);
  for(int address=0;address<0x800;address+=0x80) {
    snprintf(cmd,1024,"%x!g",address);
    write(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(20000);
    snprintf(cmd,1024,"!E");
    write(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(300000);
    eeprom_parse_output(fd,readblock);
    fprintf(stderr,"."); fflush(stderr);
  }
  fprintf(stderr,"\n"); fflush(stderr);

  FILE *f=fopen("/tmp/eeprom.data","w");
  if (!f) f=stderr;
  eeprom_decode_data("Datablock read from EEPROM",readblock,f);
  if (f!=stderr) fclose(f);
  
  return 0;      
}
