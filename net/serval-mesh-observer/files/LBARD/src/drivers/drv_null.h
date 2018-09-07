int null_radio_detect(int fd);
int null_serviceloop(int serialfd);
int null_receive_bytes(unsigned char *bytes,int count);
int null_send_packet(int serialfd,unsigned char *out, int len);
int null_check_if_ready(void);

  
