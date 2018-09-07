int hfbarrett_serviceloop(int serialfd);
int hfbarrett_process_line(char *l);
int hfbarrett_receive_bytes(unsigned char *bytes,int count);
int hfbarrett_send_packet(int serialfd,unsigned char *out, int len);
int init_buffer(unsigned char* buffer, int size);
int hfbarrett_ready_test(void);
int hfbarrett_my_turn_to_send(void);
