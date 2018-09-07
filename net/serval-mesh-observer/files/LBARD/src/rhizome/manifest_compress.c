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

#include "sync.h"
#include "lbard.h"
#include "util.h"

#ifdef TEST
// Only present to satisfy the timestamp_str() function
char *my_sid_hex="NOT VALID";
#endif

// Table of fields and transformations
struct manifest_field {
  unsigned char token;
  char *name;
  // How shall the field be encoded?
  // The first applicable strategy is always used for the field type.
  unsigned char hex_bytes; // 0 = non-hex
  unsigned char int_bytes; // 0 = non-integer, 0xff = variable length encoding
  unsigned char is_enum; // if non-zero, then encode as enum
  char *enum_options;
};

struct manifest_field fields[]={
  // Hex fields
  {0x80,"id",0x20,0,0,NULL},
  {0x81,"BK",0x20,0,0,NULL},
  {0x82,"sender",0x20,0,0,NULL},
  {0x83,"recipient",0x20,0,0,NULL},

  {0x90,"filehash",0x40,0,0,NULL},

  // Numeric fields
  {0xa0,"version",0,0xff,0,NULL},
  {0xa1,"filesize",0,0xff,0,NULL},
  {0xa2,"date",0,0xff,0,NULL},
  {0xa3,"crypt",0,0xff,0,NULL},
  {0xa4,"tail",0,0xff,0,NULL},

  // enums (CASE SENSITIVE!)
  // (a comma MUST appear after the last option (it simplifies the parser)
  {0xb0,"service",0,0,1,"file,MeshMS1,MeshMS2,"},
  
  {0,NULL,0,0}
};

int field_encode(int field_number,unsigned char *key,unsigned char *value,
		 unsigned char *bin_out,int *out_offset)
{
  int offset=*out_offset;
  bin_out[offset++]=fields[field_number].token;
  
  if (fields[field_number].hex_bytes) {
    // Encode string of hex.
    // MUST be uppercase
    for(int i=0;i<fields[field_number].hex_bytes;i++) {
      int hi=chartohex(value[i*2+0]);
      int lo=chartohex(value[i*2+1]);
      if ((hi<0)||(lo<0)) return -1;
      bin_out[offset++]=(hi<<4)|lo;
    }
    *out_offset=offset;
    return 0;
  } else if (fields[field_number].int_bytes) {
    if (fields[field_number].int_bytes==0xff) {
      // Variable length encoding: 7-bits per byte has
      // value. MSB set indicates another bit follows
      long long v=strtoll((char *)value,NULL,10);
      do {
	bin_out[offset]=v&0x7f;
	v=v>>7;
	if (v) bin_out[offset]|=0x80;
	offset++;
      } while(v);
      *out_offset=offset;
      return 0;      
    } else
      return -1;
  } else if (fields[field_number].is_enum) {
    // Search through enum options and encode if we can.
    unsigned char option[1024];
    int option_len=0;
    int option_number=0;
    for(int o=0;fields[field_number].enum_options[o];o++) {
      if (fields[field_number].enum_options[o]==',') {
	option[option_len]=0;
	if (!strcmp((char *)option,(char*)value)) {
	  bin_out[offset++]=option_number;
	  *out_offset=offset;
	  return 0;      
	}
	option_len=0;
	option_number++;
      } else {
	option[option_len++]=(unsigned char)fields[field_number].enum_options[o];
      }
    }
    // Unknown enum value
    return -1;
  } else {
    // Unknown field type: should never happen
    return -1;
  }
}

int field_decode(int field_number,unsigned char *bin_in,int *in_offset,
		 unsigned char *text_out,int *out_offset)
{
  int offset=*out_offset;

  //  Make sure a malicious binary encoded manifest can't overflow output.
  if (offset>900) return -1;
  
  // output field name
  sprintf((char *)&text_out[offset],"%s=",fields[field_number].name);
  offset+=strlen(fields[field_number].name)+1;

  if (fields[field_number].hex_bytes) {
    // Decode string of hex.
    for(int i=0;i<fields[field_number].hex_bytes;i++) {
      int v=bin_in[(*in_offset)++];
      text_out[offset++]=hextochar(v>>4);
      text_out[offset++]=hextochar(v&0xf);
    }
    text_out[offset++]='\n';
    *out_offset=offset;
    return 0;
  } else if (fields[field_number].int_bytes) {
    if (fields[field_number].int_bytes==0xff) {
      unsigned long long v=0LL;
      long long shift=0;
      while(1) {
	v|=(((unsigned long long)(bin_in[(*in_offset)]&0x7f))<<shift);
	shift+=7;
	if (bin_in[(*in_offset)++]&0x80)
	  continue;
	else
	  break;
      }
      offset+=snprintf((char *)&text_out[offset],1024-offset,"%lld\n",v);
      
      *out_offset=offset;
      return 0;      
    } else
      return -1;
  } else if (fields[field_number].is_enum) {
    // Search through enum options and encode if we can.
    int selected_option=bin_in[(*in_offset)++];
    unsigned char option[1024];
    int option_len=0;
    int option_number=0;
    for(int o=0;fields[field_number].enum_options[o];o++) {
      if (fields[field_number].enum_options[o]==',') {
	option[option_len]=0;
	if (option_number==selected_option) {
	  offset+=snprintf((char *)&text_out[offset],1024-offset,"%s\n",option);
	  *out_offset=offset;
	  return 0;
	}
	option_len=0;
	option_number++;
      } else {
	option[option_len++]=(unsigned char)fields[field_number].enum_options[o];
      }
    }
    // Invalid enum value
    return -1;
  } else {
    // Unknown field type: should never happen
    return -1;
  }

}

/*
  Decode binary format manifest.
  This really consists of looking for tokens and expanding them.
  The only complications are that we need to stop looking for
  tokens once we hit the signature block, and that we should not look
  for tokens except at the start of lines, so that values can contain
  UTF-8 encoding.
 */
int manifest_binary_to_text(unsigned char *bin_in, int len_in,
			    unsigned char *text_out, int *len_out)
{
  int offset=0;
  int out_offset=0;
  int start_of_line=1;
  while(offset<len_in) {
    if (!bin_in[offset]) {
      // Copy remainder of encoded manifest out

      // Abort if overflow would occur
      if ((out_offset+len_in-offset+1)>=1024) return -1;

      bcopy(&bin_in[offset],&text_out[out_offset],len_in-offset);
      out_offset+=len_in-offset;
      offset+=len_in-offset;
    } else {
      if (start_of_line&&(bin_in[offset]&0x80)) {
	// It's a token
	int field;
	for(field=0;fields[field].token;field++) {
	  if (bin_in[offset]==fields[field].token) break;
	}
	// Fail decode if we hit an unknown token
	if (!fields[field].token) {
	  // printf("Unknown token: 0x%02x\n",bin_in[offset]);
	  return -1;
	}
	// Also fail if we cannot decode a token
	offset++;
	if (field_decode(field,bin_in,&offset,text_out,&out_offset)) {
	  // printf("Failed to decode token 0x%02x @ offset %d\n",bin_in[offset],offset);
	  return -1;
	}
	text_out[out_offset]=0;
      } else if (bin_in[offset]=='\n') {
	// new line, so remember it is the start of a line
	start_of_line=1;
	if (out_offset>1023) return -1;
	text_out[out_offset++]=bin_in[offset++];
      } else {
	// not a new line, so clear start of line flag, so that
	// we don't try to interpret UTF-8 value strings as tokens.
	start_of_line=0;
	if (out_offset>1023) return -1;
	text_out[out_offset++]=bin_in[offset++];
      }
    }
  }
  
  *len_out=out_offset;
  return 0;
}

// Produce a more compact manifest representation
// XXX - doesn't currently compress free-text fields
int manifest_text_to_binary(unsigned char *text_in, int len_in,
			    unsigned char *bin_out, int *len_out)
{
  // Manifests must be <1KB
  if (len_in>1024) return -1;

  int out_offset=0;
  int offset;
  for(offset=0;offset<len_in;offset++) {
    // See if we are at a line of KEY=VALUE format.
    unsigned char key[1024], value[1024];
    int length;
    if (sscanf((const char *)&text_in[offset],"%[^=]=%[^\n]%n",
	       key,value,&length)==2) {
      // We think we have a field
      // printf("line: [%s]=[%s] (out_offset=%d)\n",key,value,out_offset);
      // See if we know about this field to binary encode it:
      int f=0;
      for(f=0;fields[f].token;f++)
	if (!strcasecmp((char *)key,fields[f].name)) {
	  // It is this field
	  break;
	}
      if ((!fields[f].token)
	  ||(field_encode(f,key,value,bin_out,&out_offset)))
	{
	  // Could not encode the field compactly, so just copy it out.
	  int count=sprintf((char *)&bin_out[out_offset],"%s=%s\n",(char *)key,(char *)value);
	  out_offset+=count;
	}
      // Skip remainder of the line
      // (the for loop will add one, which skips the \n character)
      offset+=length;
    } else {
      // Is not a key value pair: just copy the character ...
      // ... unless it is 0x00, in which case it marks the start of the
      // binary section, which we should just copy verbatim, and then
      // break out of the loop.
      if (text_in[offset]==0x00) {
	int count=len_in-offset;
	bcopy(&text_in[offset],&bin_out[out_offset],count);
	out_offset+=count;
	break;
      } else {
	bin_out[out_offset++]=text_in[offset];
      }
    }
  }

#ifdef TEST
  {
    FILE *f=fopen("test.bmanifest","w");
    fwrite(bin_out,out_offset,1,f);
    fclose(f);
  }

  printf("Text input length = %d, binary version length = %d\n",
	 len_in,out_offset);
#endif

  // Now verify that we can decode it correctly (otherwise signatures will
  // fail)
  unsigned char verify_out[1024];
  int verify_length=0;
  manifest_binary_to_text(bin_out,out_offset,verify_out,&verify_length);

#ifdef TEST
  {
    FILE *f=fopen("verify.manifest","w");
    fwrite(verify_out,verify_length,1,f);
    fclose(f);
  }
#endif


  if ((verify_length!=len_in)
      ||bcmp(text_in,verify_out,len_in)) {
#ifdef TEST
    printf("Verify error with binary manifest: reverting to plain text.\n");
    printf("  decoded to %d bytes (should be %d)\n",
	   verify_length,len_in);
#endif
    bcopy(text_in,bin_out,len_in);
    *len_out=len_in;
    return -1;
  } else {
    // Encoding was successful
    *len_out=out_offset;
    return 0;
  }

}

#ifdef TEST
int main(int argc,char **argv)
{
  if (argc!=2) {
    fprintf(stderr,"Test manifest binary representation conversion code.\n");
    fprintf(stderr,"usage: manifesttest <manifest>\n");
    exit(-1);
  }

  FILE *f=fopen(argv[1],"r");
  if (!f) {
    fprintf(stderr,"Could not read from '%s'\n",argv[1]);
    exit(-1);
  }
  unsigned char text_in[8192];
  int in_len=fread(text_in,1,8192,f);
  fprintf(stderr,"Read %d bytes.\n",in_len);  
  fclose(f);
  if ((in_len>1023)||(in_len<100)) {
    fprintf(stderr,"Manifest too short or too long. Must be between 100 and 1023 bytes.\n");
    exit(-1);
  }

  unsigned char bin_out[1024];
  int bin_len=0;
  int r=manifest_text_to_binary(text_in,in_len,bin_out,&bin_len);
  fprintf(stderr,"Encoding return value = %d.\n",r);
  fprintf(stderr,"Binary encoding of manifest requires %d bytes.\n",bin_len);
}
#endif

int manifest_get_field(unsigned char *manifest, int manifest_len,
		       char *fieldname,
		       char *field_value)
{
  int offset;
  field_value[0]=0;
  for(offset=0;offset<manifest_len;offset++) {
    // See if we are at a line of KEY=VALUE format.
    unsigned char key[1024];
    int length;
    if (sscanf((const char *)&manifest[offset],"%[^=]=%[^\n]%n",
	       key,field_value,&length)==2) {
      if (!strcasecmp((char *)key,fieldname))
	return 0;
      else field_value[0]=0;
    }
  }
  return -1;
}
