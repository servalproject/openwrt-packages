int chartohex(int c);
int hextochar(int h);
int nybltohexchar(int v);
int ishex(int c);
int chartohexnybl(int c);
int hex_encode(unsigned char *in, char *out, int in_len, int radio_type);
int hex_decode(char *in, unsigned char *out, int out_len,int radio_type);
int ascii64_encode(unsigned char *in, char *out, int in_len, int radio_type);
int ascii64_decode(char *in, unsigned char *out, int out_len,int radio_type);
int dump_bytes(FILE *f,char *msg, unsigned char *bytes, int length);
char *timestamp_str(void);

