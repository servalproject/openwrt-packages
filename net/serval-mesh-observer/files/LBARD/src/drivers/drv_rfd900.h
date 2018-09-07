int always_ready(void);
int rfd900_serviceloop(int serialfd);
int rfd900_receive_bytes(unsigned char *bytes,int count);
int rfd900_radio_detect(int fd);
int rfd900_set_tx_power(int serialfd);
int rfd900_send_packet(int serialfd,unsigned char *out, int offset);
