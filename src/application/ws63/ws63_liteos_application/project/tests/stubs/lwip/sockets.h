#ifndef TEST_SOCKETS_H
#define TEST_SOCKETS_H
#include <stddef.h>
#include <stdint.h>
#define AF_INET 2
#define SOCK_STREAM 1
#define INADDR_NONE 0xFFFFFFFFUL
#define SOL_SOCKET 1
#define SO_RCVTIMEO 2
struct in_addr { unsigned long s_addr; };
struct sockaddr_in { int sin_family; uint16_t sin_port; struct in_addr sin_addr; };
struct sockaddr { int family; };
struct timeval { long tv_sec, tv_usec; };
int socket(int family,int type,int protocol);
unsigned long inet_addr(const char *address);
uint16_t htons(uint16_t value);
int connect(int fd,const struct sockaddr *address,unsigned int length);
int setsockopt(int fd,int level,int name,const void *value,unsigned int length);
int send(int fd,const void *data,int length,int flags);
int recv(int fd,void *data,int length,int flags);
#endif
