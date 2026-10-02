#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "clearchain_http.h"
#include "clearchain_key.h"
#include "clearchain_runtime_config.h"
static unsigned int network_calls;
int socket(int a,int b,int c) { (void)a;(void)b;(void)c;network_calls++;return -1; }
unsigned long inet_addr(const char *s) { (void)s;network_calls++;return 0; }
uint16_t htons(uint16_t v) { network_calls++;return v; }
int connect(int f,const struct sockaddr *s,unsigned int n) { (void)f;(void)s;(void)n;network_calls++;return -1; }
int setsockopt(int f,int l,int n,const void *v,unsigned int s) { (void)f;(void)l;(void)n;(void)v;(void)s;network_calls++;return -1; }
int send(int f,const void *p,int n,int g) { (void)f;(void)p;(void)n;(void)g;network_calls++;return -1; }
int recv(int f,void *p,int n,int g) { (void)f;(void)p;(void)n;(void)g;network_calls++;return -1; }
struct hostent *gethostbyname(const char *s) { (void)s;network_calls++;return NULL; }
int TCP_CloseClient(int fd) { (void)fd;network_calls++;return 0; }
int TCP_SendData(int fd,char *s) { (void)fd;(void)s;network_calls++;return -1; }
const clearchain_stage_config_t *clearchain_key_get_stage_config(void)
{ static const clearchain_stage_config_t s={4,"test","synthetic_scanner","synthetic_code"};return &s; }
int main(void)
{
    assert(!CLEARCHAIN_UPLOAD_ALLOWED);
    r200_batch_t b={0}; b.tag_count=1;b.total_samples=1;
    strcpy(b.tags[0].chip_uid,"SYNTHETIC");b.tags[0].sample_count=1;b.tags[0].rssi_dbm[0]=-52;
    clearchain_register_response_t registered; clearchain_verify_response_t verified;
    assert(clearchain_send_scan("SYNTHETIC")<0);
    assert(clearchain_send_factory_scan("SYNTHETIC")<0);
    assert(clearchain_send_register_batch("SYNTHETIC-BATCH",&b,&registered)<0);
    assert(clearchain_send_factory_batch("SYNTHETIC-BATCH",&b,&registered)<0);
    assert(clearchain_send_verify_scan("SYNTHETIC","synthetic",&verified)<0);
    assert(network_calls==0);
    b.tag_count=33;assert(clearchain_send_register_batch("test",&b,&registered)<0);
    b.tag_count=1;b.tags[0].sample_count=5;assert(clearchain_send_factory_batch("test",&b,&registered)<0);
    assert(network_calls==0);
    puts("PASS: every HTTP endpoint returns before socket/DNS/send with upload disabled; malformed batch bounds");
    return 0;
}
