#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

int filter_and_enqueue_packet_for_client(int from,int to, long long delivery_time,
					 uint8_t *packet_in,int packet_len);
long long gettime_ms();

#include "fec-3.0.1/fixed.h"
void encode_rs_8(data_t *data, data_t *parity,int pad);
int decode_rs_8(data_t *data, int *eras_pos, int no_eras, int pad);
#define FEC_LENGTH 32
#define FEC_MAX_BYTES 223

extern long long start_time;
extern long long total_transmission_time;
extern long long first_transmission_time;

#define RADIO_RFD900 1
#define RADIO_HFCODAN 2
#define RADIO_HFBARRETT 3
#define RADIO_REAL 99

struct client {
  int socket;
  int radio_type;

  int rx_state;
#define STATE_NORMAL 0
#define STATE_BANG 1

#define CLIENT_BUFFER_SIZE 4096
  unsigned char buffer[CLIENT_BUFFER_SIZE];
  int buffer_count;

  // Buffer holding received packet ready for sending when transmission
  // time actually expires.
  unsigned char rx_queue[CLIENT_BUFFER_SIZE];
  int rx_queue_len;
  long long rx_embargo;
  int rx_colission;
  
};

#define MAX_CLIENTS 1024
extern struct client clients[MAX_CLIENTS];
extern int client_count;

int rfd900_setbitrate(char *b);
int release_pending_packets(int i);

int rfd900_read_byte(int client,unsigned char byte);
int hfcodan_read_byte(int client,unsigned char c);
int hfbarrett_read_byte(int client,unsigned char c);
int rfd900_heartbeat(int client);
int hfcodan_heartbeat(int client);
int hfbarrett_heartbeat(int client);
int rfd900_encapsulate_packet(int from,int to,unsigned char *packet,
			      int *packet_len);
int hfcodan_encapsulate_packet(int from,int to,unsigned char *packet,
			      int *packet_len);
int hfbarrett_encapsulate_packet(int from,int to,unsigned char *packet,
			      int *packet_len);


