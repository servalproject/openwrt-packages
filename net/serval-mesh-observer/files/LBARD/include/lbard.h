#include <sys/time.h>

#define SYNC_MSG_HEADER_LEN 2

// For now we use a fixed link MTU for all radio types for now.
// For ALE 2G we fragment frames.  We can revise this for ALE 3G large message blocks,
// and when we implement a better analog modem down the track.
#define LINK_MTU 200

extern struct sync_state *sync_state;
#define SYNC_SALT_LEN 8

#define SERVALD_STOP "/usr/bin/servald stop"
#define DEFAULT_BROADCAST_ADDRESSES "10.255.255.255","192.168.2.255","192.168.2.1"
#define SEND_HELP_MESSAGE "/usr/bin/servald meshms send message `/usr/bin/servald id self | tail -1` `cat /dos/helpdesk.sid` '"

#define C fprintf(stderr,"%s:%d CHECKPOINT in %s()\n",__FILE__,__LINE__,__FUNCTION__)

// Set packet TX interval details (all in ms)
#define INITIAL_AVG_PACKET_TX_INTERVAL 1000
#define INITIAL_PACKET_TX_INTERVAL_RANDOMNESS 250

/* Ideal number of packets per 4 second period.
   128K air interface with 256 byte (=2K bit) packets = 64 packets per second.
   But we don't want to go that high for a few reasons:
   1. It would crash the FTDI serial drivers on Macs (and I develop on one)
   2. With a simple CSMA protocol, we should aim to keep below 30% channel
      utilisation to minimise colissions.
   3. We are likely to have hidden sender problems.
   4. We don't want to suck too much power.

   So we will aim to keep channel utilisation down around 10%.
   64 * 10% * 4 seconds = 26 packets per 4 seconds.
 */
// 52 = target 20% channel utilisation
#define TARGET_TRANSMISSIONS_PER_4SECONDS 26
extern int target_transmissions_per_4seconds;

// BAR consists of:
// 8 bytes : BID prefix
// 8 bytes : version
// 4 bytes : recipient prefix
// 1 byte : size and meshms flag byte
#define BAR_LENGTH (8+8+4+1)

struct segment_list {
  unsigned char *data;
  int start_offset;
  int length;
  struct segment_list *prev,*next;
};

struct recent_sender {
  unsigned char sid_prefix[2];
  time_t last_time;
};

#define MAX_RECENT_SENDERS 16
struct recent_senders {
  struct recent_sender r[MAX_RECENT_SENDERS];
};

struct partial_bundle {
  // Data from the piece headers for keeping track
  char *bid_prefix;
  long long bundle_version;

  int recent_bytes;
  
  struct segment_list *manifest_segments;
  int manifest_length;

  struct segment_list *body_segments;
  int body_length;

  struct recent_senders senders;

  // Request bitmap for body and for manifest
  int request_bitmap_start;
  unsigned char request_bitmap[32];
  unsigned char request_manifest_bitmap[2];
};

#define DEFAULT_PEER_KEEPALIVE_INTERVAL 20
extern int peer_keepalive_interval;

struct peer_state {
  char *sid_prefix;
  unsigned char sid_prefix_bin[4];

  // random 32 bit instance ID, used to work out when LBARD has died and restarted
  // on a peer, so that we can restart the sync process.
  unsigned int instance_id;
  
  unsigned char *last_message;
  time_t last_message_time;
  // if last_message_time is more than this many seconds ago, then they aren't
  // considered an active peer, and are excluded from rhizome rank calculations
  // and various other things.
  int last_message_number;

  time_t last_timestamp_received;

  // Used to log RSSI of receipts from this sender, so that we can show in the stats display
  int rssi_accumulator;
  int rssi_counter;
  // Used to show number of missed packets in the stats display
  int missed_packet_count;

  // Enough for 2 packets per second for a full minute
#define RSSI_LOG_SIZE 120
  int rssi_log_count;
  int recent_rssis[RSSI_LOG_SIZE];
  long long recent_rssi_times[RSSI_LOG_SIZE];
  
#ifdef SYNC_BY_BAR
  // BARs we have seen from them.
  int bundle_count;
#define MAX_PEER_BUNDLES 100000
  int bundle_count_alloc;
  char **bid_prefixes;
  long long *versions;
  unsigned char *size_bytes;
  unsigned char *insert_failures;
#else

  // Bundle we are currently transfering to this peer
  int tx_bundle;
  int tx_bundle_priority;
  int tx_bundle_manifest_offset;
  int tx_bundle_body_offset;

  // These get set to offsets provided in an ACK('A') packet,
  // so that we avoid resending stuff that has been definitively acknowledged by
  // the recipient.  The values from the ACK are written directly in here, so that
  // if a problem does arise, the recipient can move the ACK point backwards if
  // required
  int tx_bundle_manifest_offset_hard_lower_bound;
  int tx_bundle_body_offset_hard_lower_bound;

  // number of http fetch errors for a manifest/payload we tolerate, before
  // discarding this bundle and trying to send the next.
#define MAX_CACHE_ERRORS 5
  int tx_cache_errors;

  /* Bundles we want to send to this peer
     In theory, we can have a relatively short queue, since we intend to rebuild the
     tree periodically. Thus any bundles received will cause the new sync process to
     not insert those in the TX queue, and thus allow the transfer of the next 
     MAX_TXQUEUE_LEN highest priority bundles. */
#define MAX_TXQUEUE_LEN 10
  int tx_queue_len; 
  int tx_queue_bundles[MAX_TXQUEUE_LEN];
  unsigned int tx_queue_priorities[MAX_TXQUEUE_LEN];
  int tx_queue_overflow;
#endif

  /* Bitmaps that we use to keep track of progress of sending a bundle.
     We mark off blocks as we send them, or as we see them TXd by others,
     or as we get an explicit bitmap state sent by the receiver.
     
     A set bit means that we have received that 64 byte piece. */
  int request_bitmap_bundle;
  int request_bitmap_offset;
  unsigned char request_bitmap[32];
  unsigned char request_manifest_bitmap[2];
};

// Bundles this peer is transferring.
// The bundle prioritisation algorithm means that the peer may announce pieces
// of several bundles interspersed, e.g., a large bundle may be temporarily
// deferred due to the arrival of a smaller bundle, or the arrival of a peer
// for whom that peer has bundles with the new peer as the recipient.
// So we need to carry state for some plurality of bundles being announced.
// Note that we don't (currently) build bundles from announcements by multiple
// peers, as a small protection against malicious nodes offering fake bundle
// pieces that will result in the crypto checksums failing at the end.
#define MAX_BUNDLES_IN_FLIGHT 256
struct partial_bundle partials[MAX_BUNDLES_IN_FLIGHT];  

struct recent_bundle {
  char *bid_prefix;
  long long bundle_version;
  time_t timeout;
};

typedef int (*message_handler)(struct peer_state *,char *,
			       char *, char *,unsigned char *,int);  
extern message_handler message_handlers[257];

extern int txpower;
extern int txfreq;

extern int serial_errors;

extern int radio_temperature;
extern char *otabid;
extern char *otadir;

extern long long start_time;
extern int my_time_stratum;
extern int radio_transmissions_byus;
extern int radio_transmissions_seen;
extern long long last_message_update_time;
extern long long next_message_update_time;
extern long long congestion_update_time;
extern int message_update_interval;
extern int message_update_interval_randomness;

extern int monitor_mode;

extern char message_buffer[];
extern int message_buffer_size;
extern int message_buffer_length;

extern char *my_signingid_hex;
extern char *my_sid_hex;
extern unsigned char my_sid[32];

struct bundle_record {
  int index; // position in array of bundles
  
  char *service;
  char *bid_hex;
  unsigned char bid_bin[32];
  long long version;
  char *author;
  int originated_here_p;
#ifdef SYNC_BY_BAR
#define TRANSMIT_NOW_TIMEOUT 2
  time_t transmit_now;
  int announce_bar_now;
#else
  sync_key_t sync_key;
#endif
  long long length;
  char *filehash;
  char *sender;
  char *recipient;

  // The last time we announced this bundle in full.
  time_t last_announced_time;
  // The last version of the bundle that we announced.
  long long last_version_of_manifest_announced;
  // The furthest through the file that we have announced during the current
  // attempt at announcing it (which may be interrupted by the presence of bundles
  // with a higher priority).
  long long last_offset_announced;
  // Similarly for the manifest
  long long last_manifest_offset_announced;
  
  long long last_priority;
  int num_peers_that_dont_have_it;
};

// New unified BAR + optional bundle record for BAR tree structure
typedef struct bar_data {
  // BAR fields
  long long bid_prefix_bin;
  long long version;
  long long recipient_sid_prefix_bin;
  int size_byte;
  
} bar_data;

typedef struct bundle_data {
  // Bundle fields, only valid if have_bundle != 0
  // All fields are initialised as fixed-length strings so that we can avoid
  // malloc.
  char service[40];
  char bid_hex[32*2+1];
  char author[32*2+1];
  int originated_here_p;
  char filehash[64*2+1];
  char sender[32*2+1];
  char recipient[32*2+1];
} bundle_data;
  
typedef struct bundle_node {
  // Do we have the bundle?
  // If the node exists, we must have the BAR, because we can make the BAR from a
  // locally stored bundle.  However, a BAR received from a peer can be present,
  // without us having the bundle in Rhizome (yet).
  int have_bundle;
  int have_manifest;
  int have_payload;

  bar_data *bar;
  bundle_data *bundle;
  
  // Priority of bundle based on attributes
  int intrinsic_priority;

  // XOR of all BARs of nodes below this one.
  unsigned char node_xor[BAR_LENGTH];
  
  // Links to other elements in the tree
  struct bundle_node *parent,*left, *right;
} bundle_node;

#define REPORT_QUEUE_LEN 32
#define MAX_REPORT_LEN 64
extern int report_queue_length;
extern uint8_t report_queue[REPORT_QUEUE_LEN][MAX_REPORT_LEN];
extern uint8_t report_lengths[REPORT_QUEUE_LEN];
extern struct peer_state *report_queue_peers[REPORT_QUEUE_LEN];
extern int report_queue_partials[REPORT_QUEUE_LEN];
extern char *report_queue_message[REPORT_QUEUE_LEN];


extern unsigned int my_instance_id;

#define MAX_PEERS 1024
extern struct peer_state *peer_records[MAX_PEERS];
extern int peer_count;

#define MAX_BUNDLES 10000
extern struct bundle_record bundles[MAX_BUNDLES];
extern int bundle_count;

extern int fresh_bundles[MAX_BUNDLES];
extern int fresh_bundle_count;

extern char *bid_of_cached_bundle;
extern long long cached_version;
// extern int cached_manifest_len;
// extern unsigned char *cached_manifest;
extern int cached_manifest_encoded_len;
extern unsigned char *cached_manifest_encoded;
extern int cached_body_len;
extern unsigned char *cached_body;

extern unsigned int option_flags;
#define FLAG_NO_RANDOMIZE_REDIRECT_OFFSET 1
#define FLAG_NO_RANDOMIZE_START_OFFSET 2
#define FLAG_NO_BITMAP_PROGRESS 4
#define FLAG_NO_HARD_LOWER 8

extern FILE *debug_file;
extern int debug_bundles;
extern int debug_bitmap;
extern int debug_http;
extern int debug_radio;
extern int debug_pieces;
extern int debug_ack;
extern int debug_announce;
extern int debug_pull;
extern int debug_insert;
extern int debug_radio_rx;
extern int debug_radio_tx;
extern int debug_gpio;
extern int debug_insert;
extern int debug_message_pieces;
extern int debug_sync;
extern int debug_sync_keys;
extern int debug_bundlelog;
extern char *bundlelog_filename;
extern int debug_noprioritisation;
extern int radio_silence_count;
extern int meshms_only;
extern int fix_badfs;
extern long long min_version;
extern int time_slave;
extern long long start_time;

// Details of the servald server we are communicating with
extern char *servald_server;
extern char *credential;
extern char *prefix;

extern long long radio_last_heartbeat_time;
extern int radio_temperature;
extern time_t last_status_time;
extern long long last_servald_contact;


int saw_piece(char *peer_prefix,int for_me,
	      char *bid_prefix, unsigned char *bid_prefix_bin,
	      long long version,
	      long long piece_offset,int piece_bytes,int is_end_piece,
	      int is_manifest_piece,unsigned char *piece,

	      char *prefix, char *servald_server, char *credential);
int saw_length(char *peer_prefix,char *bid_prefix,long long version,
	       int body_length);
int saw_message(unsigned char *msg,int len,int rssi,char *my_sid,
		char *prefix, char *servald_server,char *credential);
int load_rhizome_db(int timeout,
		    char *prefix, char *serval_server,
		    char *credential, char **token);
int parse_json_line(char *line,char fields[][8192],int num_fields);
int rhizome_update_bundle(unsigned char *manifest_data,int manifest_length,
			  unsigned char *body_data,int body_length,
			  char *servald_server,char *credential);
int prime_bundle_cache(int bundle_number,char *prefix,
		       char *servald_server, char *credential);
int hex_byte_value(char *hexstring);
int find_highest_priority_bundle(void);
int find_highest_priority_bar(void);
int find_peer_by_prefix(char *peer_prefix);
int clear_partial(struct partial_bundle *p);
int dump_partial(struct partial_bundle *p);
int merge_segments(struct segment_list **s);
int free_peer(struct peer_state *p);
int peer_note_bar(struct peer_state *p,
		  char *bid_prefix,long long version, char *recipient_prefix,
		  int size_byte);
int announce_bundle_piece(int bundle_number,int *offset,int mtu,unsigned char *msg,
			  char *prefix,char *servald_server, char *credential,
			  int target_peer);
int update_my_message(int serialfd,
		      unsigned char *my_sid, char *my_sid_hex,
		      int mtu,unsigned char *msg_out,
		      char *servald_server,char *credential);
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream);
int radio_send_message(int serialfd, unsigned char *msg_out,int offset);
int radio_receive_bytes(unsigned char *buffer, int bytes, int monitor_mode);
ssize_t write_all(int fd, const void *buf, size_t len);
int radio_read_bytes(int serialfd, int monitor_mode);
ssize_t read_nonblock(int fd, void *buf, size_t len);

int http_get_simple(char *server_and_port, char *auth_token,
		    char *path, FILE *outfile, int timeout_ms,
		    long long *last_read_time, int outputheaders);
int http_post_bundle(char *server_and_port, char *auth_token,
		     char *path,
		     unsigned char *manifest_data, int manifest_length,
		     unsigned char *body_data, int body_length,
		     int timeout_ms);
long long gettime_ms(void);
long long gettime_us(void);
int generate_progress_string(struct partial_bundle *partial,
			     char *progress,int progress_size);
int show_progress(FILE *f,int verbose);
int show_progress_json(FILE *f,int verbose);
int request_wanted_content_from_peers(int *offset,int mtu, unsigned char *msg_out);
int dump_segment_list(struct segment_list *s);

int energy_experiment(char *port, char *interface_name,char *broadcast_address);
int energy_experiment_master(char *broadcast_address,
			     char *backchannel_address,char *parameters);
int energy_experiment_calibrate(char *port, char *broadcast_address,char *exp_string);

int serial_setup_port_with_speed(int fd,int speed);
int status_dump(void);
int status_log(char *msg);

long long calculate_bundle_intrinsic_priority(char *bid,
					      long long length,
					      long long version,
					      char *service,
					      char *recipient,
					      int insert_failures);
int bid_to_peer_bundle_index(int peer,char *bid_hex);
int manifest_extract_bid(unsigned char *manifest_data,char *bid_hex);
int we_have_this_bundle_or_newer(char *bid_prefix, long long version);
int register_bundle(char *service,
		    char *bid,
		    char *version,
		    char *author,
		    char *originated_here,
		    long long length,
		    char *filehash,
		    char *sender,
		    char *recipient,
		    char *name);
long long size_byte_to_length(unsigned char size_byte);
char *bundle_recipient_if_known(char *bid_prefix);
int rhizome_log(char *service,
		char *bid,
		char *version,
		char *author,
		char *originated_here,
		long long length,
		char *filehash,
		char *sender,
		char *recipient,
		char *message);
int manifest_text_to_binary(unsigned char *text_in, int len_in,
			    unsigned char *bin_out, int *len_out);
int manifest_binary_to_text(unsigned char *bin_in, int len_in,
			    unsigned char *text_out, int *len_out);
int manifest_get_field(unsigned char *manifest, int manifest_len,
		       char *fieldname,
		       char *field_value);
int monitor_log(char *sender_prefix, char *recipient_prefix,char *msg);
int bytes_to_prefix(unsigned char *bytes_in,char *prefix_out);
int saw_timestamp(char *sender_prefix,int stratum, struct timeval *tv);
int http_process(struct sockaddr *cliaddr,
		 char *servald_server,char *credential,
		 char *my_sid_hex,
		 int socket);
int chartohex(int c);
int random_active_peer(void);
int append_bytes(int *offset,int mtu,unsigned char *msg_out,
		 unsigned char *data,int count);
int sync_tree_receive_message(struct peer_state *p, unsigned char *msg);
int lookup_bundle_by_sync_key(uint8_t bundle_sync_key[KEY_LEN]);
int peer_queue_bundle_tx(struct peer_state *p,struct bundle_record *b, int priority);
int sync_parse_ack(struct peer_state *p,unsigned char *msg,
		   char *sid_prefix_hex,
		   char *servald_server, char *credential);
int http_post_meshms(char *server_and_port, char *auth_token,
		     char *message,char *sender,char *recipient,
		     int timeout_ms);
int http_meshmb_post(char *server_and_port, char *auth_token,
		     char *sender, char *message, int timeout_ms);
int http_meshmb_activity(char *server_and_port, char *auth_token,
			 char *id_hex,int timeout_ms);
int http_meshmb_activity_since(char *server_and_port,
			       char *auth_token,
			       char *id_hex,char *token,
			       int timeout_ms);
int http_meshmb_follow(char *server_and_port, char *auth_token,
		       char *me_hex,char *you_hex,int timeout_ms);
int http_meshmb_ignore(char *server_and_port, char *auth_token,
		       char *me_hex,char *you_hex,int timeout_ms);
int http_meshmb_block(char *server_and_port, char *auth_token,
		       char *me_hex,char *you_hex,int timeout_ms);
int http_meshmb_list_following(char *server_and_port,
			       char *auth_token,
			       char *id_hex,int timeout_ms);
int http_meshmb_read(char *server_and_port,
		     char *auth_token,
		     char *id_hex,int timeout_ms);

int sync_setup(void);
int sync_by_tree_stuff_packet(int *offset,int mtu, unsigned char *msg_out,
			      char *sid_prefix_hex,
			      char *servald_server,char *credential);
int sync_tell_peer_we_have_this_bundle(int peer, int bundle);
int sync_tell_peer_we_have_the_bundle_of_this_partial(int peer, int partial);
int sync_queue_bundle(struct peer_state *p,int bundle);
int sync_schedule_progress_report(int peer, int partial, int randomJump);
int sync_schedule_progress_report_bitmap(int peer, int partial);
int bundle_calculate_tree_key(sync_key_t *sync_key,
			      uint8_t sync_tree_salt[SYNC_SALT_LEN],
			      char *bid,
			      long long version,
			      long long length,
			      char *filehash);
int dump_bytes(FILE *f,char *msg,unsigned char *bytes,int length);
int urandombytes(unsigned char *buf, size_t len);
int active_peer_count(void);
int sync_dequeue_bundle(struct peer_state *p,int bundle);
int meshms_parse_command(int argc,char **argv);
int meshmb_parse_command(int argc,char **argv);
int http_list_meshms_conversations(char *server_and_port, char *auth_token,
				   char *participant,int timeout_ms);
int http_list_meshms_messages(char *server_and_port, char *auth_token,
			      char *sender, char *recipient,int timeout_ms);
int http_send_meshms_message(char *server_and_port, char *auth_token,
			     char *sender, char *recipient,char *message,
			     int timeout_ms);
int hextochar(int h);
int peer_queue_list_dump(struct peer_state *p);
int sync_remember_recently_received_bundle(char *bid_prefix, long long version);
int sync_is_bundle_recently_received(char *bid_prefix, long long version);
int sync_tell_peer_we_have_bundle_by_id(int peer,unsigned char *bid_bin,
					long long version);
unsigned char *bid_prefix_hex_to_bin(char *hex);
int progress_report_bundle_receipts(FILE *f);
int progress_log_bundle_receipt(char *bid_prefix, long long version);

int http_get_async(char *server_and_port, char *auth_token,
		   char *path, int timeout_ms);
int http_read_next_line(int sock, char *line, int *len, int maxlen);
int load_rhizome_db_async(char *servald_server,
			  char *credential, char *token);

int lookup_bundle_by_prefix_bin_and_version_exact(unsigned char *prefix, long long version);
int lookup_bundle_by_prefix_bin_and_version_or_older(unsigned char *prefix, long long version);
int lookup_bundle_by_prefix_bin_and_version_or_newer(unsigned char *prefix, long long version);

int radio_set_type(int radio_type);
int radio_set_feature(int bitmask);
int radio_get_type(void);

int uhf_rfd900_setup(int fd);
int uhf_serviceloop(int fd);
int radio_send_message_rfd900(int serialfd,unsigned char *out, int offset);
int uhf_receive_bytes(unsigned char *bytes,int count);

int hf_parse_linkcandidate(char *description);
int hf_serviceloop(int fd);
int hf_codan_receive_bytes(unsigned char *bytes,int count);
int radio_send_message_codanhf(int serialfd,unsigned char *out, int len);
int hf_barrett_receive_bytes(unsigned char *bytes,int count);
int radio_send_message_barretthf(int serialfd,unsigned char *out, int len);

int saw_packet(unsigned char *packet_data,int packet_bytes,int rssi,
	       char *my_sid_hex,char *prefix,
	       char *servald_server,char *credential);
int radio_ready(void);
int hf_radio_ready(void);
int hf_radio_pause_for_turnaround(void);
int hf_radio_send_now(void);
int eeprom_read(int fd);
int http_report_network_status(int socket,char *topic);
int http_report_network_status_json(int socket);
int http_send_file(int socket,char *filename,char *mime_type);
int send_status_home_page(int socket);

char *find_sender_name(char *sender);

char *timestamp_str(void);
int _report_file(const char *filename,const char *file,
		 const int line,const char *function);
#define report_file(X) _report_file(X,__FILE__,__LINE__,__FUNCTION__)
int partial_update_recent_senders(struct partial_bundle *p,char *sender_prefix_hex);
int partial_update_request_bitmap(struct partial_bundle *p);
int partial_find_missing_byte(struct segment_list *s,int *isFirstMissingByte);
int hex_to_val(int c);
int sync_parse_progress_bitmap(struct peer_state *p,unsigned char *msg,int *offset);
int dump_progress_bitmap(FILE *f, unsigned char *b,int blocks);
int peer_update_request_bitmaps_due_to_transmitted_piece(int bundle_number,
							 int is_manifest,
							 int start_offset,
							 int bytes);
int peer_update_send_point(int peer);
int process_ota_bundle(char *bid,char *version);
int setup_periodic_requests(char *filename);
int make_periodic_requests(void);
int lookup_bundle_by_prefix(const unsigned char *prefix,int len);
int progress_bitmap_translate(struct peer_state *p,int new_body_offset);
int dump_peer_tx_bitmap(int peer);
int announce_bundle_length(int mtu, unsigned char *msg,int *offset,
			   unsigned char *bid_bin,long long version,unsigned int length);
int append_timestamp(unsigned char *msg_out,int *offset);
int sync_append_some_bundle_bytes(int bundle_number,int start_offset,int len,
				  unsigned char *p, int is_manifest,
				  int *offset,int mtu,unsigned char *msg,
				  int target_peer);
int sync_tree_send_message(int *offset,int mtu, unsigned char *msg_out);
int sync_build_bar_in_slot(int slot,unsigned char *bid_bin,
			   long long bundle_version);
int append_generationid(unsigned char *msg_out,int *offset);

int account_time_pause();
int account_time_resume();
int account_time(char *source);
int show_time_accounting(FILE *f);

int log_rssi(struct peer_state *p,int rssi);
int log_rssi_timewarp(long long delta);
int log_rssi_graph(FILE *f,struct peer_state *p);

#define RESOLVE_SIDS 1
int describe_bundle(int fn, FILE *f,FILE *bundlelogfile,int bn,int peerid,
		    int manifest_offset,int body_offset);

int stun_serviceloop(void);
int autodetect_radio_type(int fd);
int outernet_rx_setup(char *socket_filename);
int outernet_rx_serviceloop(void);
int set_nonblock(int fd);

#include "util.h"
