#ifndef TEST_SOCKETS_H
#define TEST_SOCKETS_H
#include <stddef.h>
#include <stdint.h>
#define AF_INET 2
#define SOCK_STREAM 1
#define INADDR_NONE 0xFFFFFFFFUL
#define SOL_SOCKET 1
#define SO_RCVTIMEO 2
#define SO_SNDTIMEO 3
#define SO_ERROR 4
#define FIONBIO 5
typedef unsigned int socklen_t;
typedef struct { unsigned long bits; } fd_set;
#define FD_ZERO(p) ((p)->bits=0)
#define FD_SET(n,p) ((p)->bits |= 1UL<<(n))
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
int lwip_ioctl(int fd,long cmd,void *value);
int select(int count,fd_set *readset,fd_set *writeset,fd_set *errors,struct timeval *timeout);
int getsockopt(int fd,int level,int opt,void *value,socklen_t *length);
#endif
