
#define HF_DISCONNECTED 1
#define HF_CALLREQUESTED 2
#define HF_CONNECTING 3
#define HF_ALELINK 4
#define HF_DISCONNECTING 5
#define HF_ALESENDING 6
#define HF_RADIOCONFUSED 7
#define HF_COMMANDISSUED 0x100

#define RADIO_ALE_2G (1<<0)
#define RADIO_ALE_3G (1<<1)

struct hf_station {
	char index[3]; //2 digits matching the ALE ID and the name of the radio (alias) in (internal radio settings)  
	char name[64]; //alias
  int link_time_target; // minutes
  int line_time_interval; // hours

  // Next target link time
  // (calculated using a pro-rata extension of line_time_interval based on the
  // duration of the last link).
  time_t next_link_time;

  // Time for next hangup, based on aiming for a call to have a maximum duration of
  // linke_time_target.
  time_t hangup_time;

  // How many successive failures in connecting to this station
  // (used to condition the selection of which station to talk to.  Basically if we
  // keep failing to connect, then we will be more likely to try other stations first)
  int consecutive_connection_failures;
};

#define MAX_HF_STATIONS 1024

extern int hf_state;
extern int hf_link_partner;

extern time_t hf_next_call_time;

extern time_t last_link_probe_time;

extern time_t hf_next_packet_time;
extern time_t last_outbound_call;

extern int hf_callout_duty_cycle;
extern int hf_callout_interval; // minutes

extern struct hf_station hf_stations[MAX_HF_STATIONS];
extern int hf_station_count;
extern struct hf_station self_hf_station;

extern int has_hf_plan;

extern time_t last_ready_report_time;

extern int ale_inprogress;
extern char hf_response_line[1024];
extern int hf_rl_len;

extern int hf_message_sequence_number;


int hf_radio_check_if_ready(void);
int hf_radio_mark_ready(void);
int hf_next_station_to_call(void);
int hf_radio_pause_for_turnaround(void);
int hf_process_fragment(char *fragment);
char *radio_type_name(int radio_type);
char *radio_type_description(int radio_type);
