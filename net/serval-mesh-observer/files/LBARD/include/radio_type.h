typedef struct radio_type {
  int id;
  char *name;
  char *description;
  int (*autodetect)(int /* fd */);
  int (*serviceloop)(int /* fd */);
  int (*receive_bytes)(unsigned char * /* bytes */, int /* count */);
  int (*send_packet)(int /* fd */,unsigned char * /* packet */,int /* length */);
  int (*is_radio_ready)(void);
  int hf_turnaround_delay;
} radio_type;

extern radio_type radio_types[];
