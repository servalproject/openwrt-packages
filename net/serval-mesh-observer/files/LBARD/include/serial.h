ssize_t write_all(int fd, const void *buf, size_t len);
int serial_setup_port(int fd);
ssize_t read_nonblock(int fd, void *buf, size_t len);
int set_block(int fd);
int set_nonblock(int fd);

#include "fec-3.0.1/fixed.h"
void encode_rs_8(data_t *data, data_t *parity,int pad);
int decode_rs_8(data_t *data, int *eras_pos, int no_eras, int pad);

