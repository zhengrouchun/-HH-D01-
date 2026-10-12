/* Enabled transport: fragmented reads/writes and bounded connect/error paths. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "clearchain_device_http.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
static char response[6000],request[6000];
static size_t cursor,sent;
static int closes,timeout_connect,fail_send;
static const char *state_json="{\"ok\":true,\"state_version\":7,\"mode\":\"S1\",\"mode_label\":\"S1 FACTORY\","
 "\"phase\":\"SCANNING\",\"armed\":true,\"batch_id\":\"BATCH-TEST\",\"tags_read\":1,\"tags_expected\":null,"
 "\"progress_percent\":null,\"screen_status\":\"NONE\",\"color\":null,\"scan_direction\":null,\"risk_percent\":0,\"message\":\"Scanning\"}";
int osal_printk(const char *f,...) { (void)f;return 0; }
uint64_t uapi_systick_get_ms(void) { return 0; }
int socket(int a,int b,int c) { (void)a;(void)b;(void)c;return 3; }
unsigned long inet_addr(const char *s) { (void)s;return 0; }
uint16_t htons(uint16_t v) { return v; }
int lwip_ioctl(int f,long c,void *v) { assert(f==3 && c==FIONBIO && v);return 0; }
int connect(int f,const struct sockaddr *s,unsigned int n)
{ (void)s;(void)n;assert(f==3);errno=EINPROGRESS;return -1; }
int select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *t)
{ (void)r;(void)e;assert(n==4 && w && t->tv_sec==2);return timeout_connect?0:1; }
int getsockopt(int f,int l,int o,void *v,socklen_t *n)
{ assert(f==3 && l==SOL_SOCKET && o==SO_ERROR && *n==sizeof(int));*(int *)v=0;return 0; }
int setsockopt(int f,int l,int o,const void *v,unsigned int n)
{ (void)o;(void)v;(void)n;assert(f==3 && l==SOL_SOCKET);return 0; }
int send(int f,const void *p,int n,int g)
{
    (void)g;assert(f==3);if(fail_send){return -1;}if(n>11){n=11;}
    assert(sent+(size_t)n<sizeof(request));memcpy(request+sent,p,(size_t)n);sent+=(size_t)n;request[sent]=0;return n;
}
int recv(int f,void *p,int n,int g)
{ (void)g;assert(f==3);size_t left=strlen(response)-cursor;if(!left){return 0;}
  if(n>17){n=17;}if((size_t)n>left){n=(int)left;}memcpy(p,response+cursor,(size_t)n);cursor+=(size_t)n;return n; }
struct hostent *gethostbyname(const char *s) { (void)s;return NULL; }
int TCP_CloseClient(int f) { assert(f==3);closes++;return 0; }
static void prepare(int code,const char *body)
{ snprintf(response,sizeof(response),"HTTP/1.1 %d Test\r\nContent-Length: %u\r\n\r\n%s",code,(unsigned int)strlen(body),body);
  cursor=sent=0;request[0]=0;timeout_connect=fail_send=0; }
int main(void)
{
    clearchain_device_state_t state,out;
    prepare(200,state_json);assert(clearchain_device_http_get_state(&state)==200 && state.state_version==7);
    assert(strstr(request,"GET /device/state HTTP/1.1"));
    prepare(200,state_json);assert(clearchain_device_http_command("STOP_SCAN",CLEARCHAIN_DEVICE_MODE_NONE,&out)==200);
    assert(strstr(request,"STOP_SCAN") && !strstr(request,"access_code"));
    prepare(200,state_json);assert(clearchain_device_http_select_mode(CLEARCHAIN_DEVICE_MODE_S4,&out)==200);
    assert(strstr(request,"SELECT_MODE") && strstr(request,"PUB-c72m") && strstr(request,"DEVICE"));
    cJSON *root=cJSON_Parse(state_json);cJSON_SetValuestring(cJSON_GetObjectItem(root,"phase"),"DONE");
    char *done=cJSON_PrintUnformatted(root);
    clearchain_device_reading_t reads[]={{"000078908",-52},{"000078908",-58}};
    prepare(200,done);assert(clearchain_device_http_post_scan(&state,reads,2,1,&out)==200);
    assert(out.phase==CLEARCHAIN_PHASE_DONE && strstr(request,"-52") && strstr(request,"-58"));
    cJSON_free(done);cJSON_Delete(root);
    prepare(409,"{\"ok\":false,\"error\":\"WRONG_PHASE\",\"message\":\"Stopped\"}");
    assert(clearchain_device_http_post_scan(&state,reads,2,0,&out)==409);
    prepare(200,"{}");assert(clearchain_device_http_get_state(&out)<0);
    prepare(200,state_json);timeout_connect=1;assert(clearchain_device_http_get_state(&out)<0 && !sent);
    prepare(200,state_json);fail_send=1;assert(clearchain_device_http_get_state(&out)<0);
    prepare(200,state_json);strcpy(response,"HTTP/1.1 200 OK\r\nContent-Length: 9999\r\n\r\n{}");
    assert(clearchain_device_http_get_state(&out)<0);
    assert(closes==9);
    puts("PASS: enabled firmware HTTP fragmented I/O, commands, POST returned state, signed repeats, 409, timeout, send failure and truncated response");
    return 0;
}
