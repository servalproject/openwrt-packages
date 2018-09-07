/*
  Manage the connected HF radio according to the supplied configuration.

  Configuration files look like:

10% calling duty cycle 
station "101" 5 minutes every 2 hours
station "102" 5 minutes every 2 hours
station "103" 5 minutes every 2 hours

  The calling duty cycle is calculated on an hourly basis, and counts only connected
  time. Call connections will be limited to 20% of the time, so that there is ample
  opportunity for a station to listen for incoming connections.

  A 100% duty cycle will mean that this radio will never be able to receive calls,
  so a 50% duty cycle (or better 1/n) duty cycle is probably more appropriate.

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

#include "sync.h"
#include "lbard.h"
#include "hf.h"
#include "config.h"

int hf_parse_linkcandidate(char *l)
{  
	char tmp[8192];
	int l_pointer = 6;
	int alias_size;

	while(l[l_pointer] != 0)
		{
		struct hf_station new_hf_station;
		//get index			
		str_part(tmp, l, l_pointer, 2);
		str_copy(new_hf_station.index, tmp);				

		//get alias
		// Parameters after the name will have to bo extracted as well.
		// So far name = alias
			//get the name size
		str_part(tmp, l, l_pointer + 3, 2);
		alias_size = atoi(tmp);
			//get the alias
		str_part(tmp, l, l_pointer + 5, alias_size);
		

		// **************
		// Code to extact parameters from address
		//*************
		
		str_copy(new_hf_station.name, tmp);
			//set the time to wait between call tentatives
		new_hf_station.next_link_time = 0; //should be got from the alias

		//add the station at hf_stations table
		str_part(tmp, l, l_pointer + 2, 1);
		if (!strcmp(tmp, "1")){ //self radio
			self_hf_station = new_hf_station;
		} else{ //other radio
			hf_stations[hf_station_count] = new_hf_station;
			hf_station_count++;
		}

		//pointer at the beginning of the next command
		l_pointer = l_pointer + 5 + alias_size; 

	}
	return 0;
}
/*
  int offset;
  char station_name[1024];
  int minutes,hours,seconds;

  printf("parsing HF ALE call list: %s\n",line);

    if ((line[0]=='#')||(line[0]<' ')) {
      // ignore blank lines and # comments
    } else if (sscanf(line,"wait %d seconds%n",&seconds,&offset)==1) {
      // Wait this long before making first call
      last_outbound_call=time(0)+seconds;
      hf_next_packet_time=time(0)+seconds;
    } else if (sscanf(line,"%d%% duty cycle%n",&hf_callout_duty_cycle,&offset)==1) {
      if (hf_callout_duty_cycle<0||hf_callout_duty_cycle>100) {
	fprintf(stderr,"Invalid call out duty cycle: Must be between 0%% and 100%%\n");
	fprintf(stderr,"  Offending line: %s\n",line);
	exit(-1);
      }
    } else if (sscanf(line,"call every %d minutes%n",&hf_callout_interval,&offset)==1) {
      if (hf_callout_interval<0||hf_callout_interval>10000) {
	fprintf(stderr,"Invalid call out interval: Must be between 0 and 10000 minutes\n");
	fprintf(stderr,"  Offending line: %s\n",line);
	exit(-1);
      }
    } else if (sscanf(line,"station \"%[^\"]\" %d minutes every %d hours",
		      station_name,&minutes,&hours)==3) {
      fprintf(stderr,"Registering station '%s' (%d minutes every %d hours)\n",
	      station_name,minutes,hours);
      if (hf_station_count<MAX_HF_STATIONS) {
	bzero(&hf_stations[hf_station_count],sizeof(struct hf_station));
	hf_stations[hf_station_count].name=strdup(station_name);
	hf_stations[hf_station_count].link_time_target=minutes;
	hf_stations[hf_station_count].line_time_interval=hours;
	hf_station_count++;
      } else {
	fprintf(stderr,"Too many HF stations. Reduce list or increase MAX_HF_STATIONS.\n");
	exit(-1);
      }
    } else {
      fprintf(stderr,"Unknown directive in HF radio plan file.\n");
      fprintf(stderr,"  Offending line: %s\n",line);
      exit(-1);	
    }

  has_hf_plan=1;
  fprintf(stderr,"Configured %d stations.\n",hf_station_count);
  
  return 0;

}*/

int str_part(char *str_part, char *string, int index_first_char, int length){
	int i;
	//printf("%s   %d\n", str_part, length);
	for(i=0; i<length; i++){
		str_part[i]=string[index_first_char + i];
	}
	str_part[length]='\0';
	return 0;
}

int str_copy(char *dest, char *source){
	int i = 0;
	while(source[i]!=0){
		dest[i] = source[i];
		i++;
	}
	dest[i]=0;
	return 0;
}
