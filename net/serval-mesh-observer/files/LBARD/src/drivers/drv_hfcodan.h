int hfcodanbarrett_radio_detect(int fd);
int hfcodan_serviceloop(int serialfd);
int hfcodan_process_line(char *l);
int hfcodan_receive_bytes(unsigned char *bytes,int count);
int hfcodan_send_packet(int serialfd,unsigned char *out, int len);
