int outernet_radio_detect(int fd);
int outernet_serviceloop(int serialfd);
int outernet_process_line(char *l);
int outernet_receive_bytes(unsigned char *bytes,int count);
int outernet_send_packet(int serialfd,unsigned char *out, int len);
int outernet_check_if_ready(void);
