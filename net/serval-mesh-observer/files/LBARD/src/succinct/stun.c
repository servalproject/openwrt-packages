#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <netdb.h>

int set_nonblock(int fd);

/*
A minimal command line tool that listens for incoming broadcast packets on port 4043,
and replies with the address of other peers that have sent a broadcast packet from the same
source port within the last 10 seconds.

The structure of the reply is;
[ 0x08 ]
[ 2 byte, big endian length ]
	[ 1 byte address length ]
	[ IPv4 (or in future IPv6) address ]
*/

struct socket_address{
  socklen_t addr_len;
  union{
    struct sockaddr addr;
    struct sockaddr_un un_addr;
    struct sockaddr_in in_addr;
    struct sockaddr_storage store;
    uint8_t raw[255];
  };
};

struct heard{
  struct timeval time;
  struct socket_address addr;
};

#define STUN_MSG 8
#define STUN_ADDRS 255

struct heard heard[STUN_ADDRS];

int stun_serviceloop(){
  static int fd = -1;
    if (fd < 0){
      struct sockaddr_in in_addr;
      in_addr.sin_family = AF_INET;
      in_addr.sin_addr.s_addr = INADDR_ANY;
      // XXX - We have a problem here for tests, because we hardwire
      // this port.
      in_addr.sin_port = htons(4043);

      fd = socket(in_addr.sin_family, SOCK_DGRAM, 0);
      if (fd < 0){
        fprintf(stderr, "\nsocket() = %d (%d)\n", fd, errno);
        return 1;
      }
      int r;
      if ((r = bind(fd, (struct sockaddr *)&in_addr, sizeof in_addr))<0){
        // fprintf(stderr, "\nbind() = %d (%d)\n", fd, errno);
        close(fd);
	fd=-1;
        return 1;
      }
    }
    struct socket_address addr;
    uint8_t buff[1024];
    addr.addr_len = sizeof addr.store;

    set_nonblock(fd);
    ssize_t r = recvfrom(fd, buff, sizeof buff, 0, &addr.addr, &addr.addr_len);
    if (r<0){
//      fprintf(stderr, "\nrecvfrom() = %zd (%d)\n", r, errno);
//      close(fd);
//      fd = -1;
      return -1;
    }
    fprintf(stderr,"received Succinct Data STUN packet. byte[0]=0x%02x\n",buff[0]);
    
    if (buff[0]==STUN_MSG)
	return 0;
    {
      char name[INET6_ADDRSTRLEN];
      char service[6];
      if (getnameinfo(&addr.addr, addr.addr_len, name, sizeof name, service, sizeof service, NI_NUMERICHOST|NI_NUMERICSERV)==0){
        fprintf(stderr, "Receive %s:%s", name, service);
      }
    }

    size_t offset=0;
    buff[offset++]=STUN_MSG;
    offset+=2;

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned i;
    struct heard *found=NULL;

    for (i=0;i<STUN_ADDRS;i++){
      if (addr.addr.sa_family == heard[i].addr.addr.sa_family &&
          addr.in_addr.sin_family == AF_INET &&
          addr.in_addr.sin_port == heard[i].addr.in_addr.sin_port &&
          memcmp(&addr.in_addr.sin_addr, &heard[i].addr.in_addr.sin_addr, sizeof addr.in_addr.sin_addr) == 0){
        // remember this sender in the same slot as last time
        found = &heard[i];
      } else if (addr.in_addr.sin_port == heard[i].addr.in_addr.sin_port &&
                 now.tv_sec - heard[i].time.tv_sec <= 10){
        // copy this slot into the packet buffer
        char name[INET6_ADDRSTRLEN];
        char service[6];
        if (getnameinfo(&heard[i].addr.addr, heard[i].addr.addr_len, name, sizeof name, service, sizeof service, NI_NUMERICHOST|NI_NUMERICSERV)==0){
          fprintf(stderr, ", %s:%s", name, service);
        }
        buff[offset++]=sizeof addr.in_addr.sin_addr;
        memcpy(&buff[offset], &heard[i].addr.in_addr.sin_addr, sizeof addr.in_addr.sin_addr);
        offset+=sizeof addr.in_addr.sin_addr;
      } else if(!found){
        // overwrite this slot
        found = &heard[i];
      }
    }

    if (found){
      found->addr = addr;
      found->time = now;
    }

    if (offset>3){
      int len = offset -3;
      buff[1]=(len << 8) & 0xFF;
      buff[2]=len & 0xFF;
      sendto(fd, buff, offset, 0, &addr.addr, addr.addr_len);
    }

  return 0;
}

