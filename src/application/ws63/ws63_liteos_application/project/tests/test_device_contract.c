#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "clearchain_device_http.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static unsigned int network_calls;
int socket(int a,int b,int c) { (void)a;(void)b;(void)c;network_calls++;return -1; }
unsigned long inet_addr(const char *s) { (void)s;network_calls++;return 0; }
uint16_t htons(uint16_t v) { network_calls++;return v; }
int connect(int f,const struct sockaddr *s,unsigned int n)
{ (void)f;(void)s;(void)n;network_calls++;return -1; }
int setsockopt(int f,int l,int n,const void *v,unsigned int s)
{ (void)f;(void)l;(void)n;(void)v;(void)s;network_calls++;return -1; }
int send(int f,const void *p,int n,int g)
{ (void)f;(void)p;(void)n;(void)g;network_calls++;return -1; }
int recv(int f,void *p,int n,int g)
{ (void)f;(void)p;(void)n;(void)g;network_calls++;return -1; }
struct hostent *gethostbyname(const char *s) { (void)s;network_calls++;return NULL; }
int TCP_CloseClient(int fd) { (void)fd;network_calls++;return 0; }

int lwip_ioctl(int f,long c,void *v) { (void)f;(void)c;(void)v;return -1; }
int select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *t) { (void)n;(void)r;(void)w;(void)e;(void)t;return -1; }
int getsockopt(int f,int l,int o,void *v,socklen_t *n) { (void)f;(void)l;(void)o;(void)v;(void)n;return -1; }
uint64_t uapi_systick_get_ms(void) { return 0; }
int osal_printk(const char *f,...) { (void)f;return 0; }

int main(void)
{
    clearchain_device_state_t state;
    clearchain_device_reading_t samples[2] = {{"E2801160",-52},{"E2801160",-58}};
    const char *json = "{\"armed\":true,\"mode_label\":\"S1 FACTORY\",\"color\":null,\"scan_direction\":null,"
        "\"ok\":true,\"state_version\":14,\"mode\":\"S1\","
        "\"phase\":\"SCANNING\",\"batch_id\":\"BATCH-2026-001\",\"tags_read\":1,"
        "\"tags_expected\":null,\"progress_percent\":null,\"screen_status\":\"NONE\","
        "\"risk_percent\":0,\"message\":\"Capturing...\"}";
    assert(clearchain_device_state_parse(json,&state)==0);
    assert(state.mode==CLEARCHAIN_DEVICE_MODE_S1 && state.phase==CLEARCHAIN_PHASE_SCANNING);
    assert(state.tags_expected==-1 && state.progress_percent==-1 && state.state_version==14);
    assert(strcmp(state.batch_id,"BATCH-2026-001")==0);
    char *body = clearchain_device_http_scan_json(&state,samples,2,1);
    assert(body!=NULL);
    cJSON *root=cJSON_Parse(body);
    assert(root!=NULL);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"final")));
    cJSON *readings=cJSON_GetObjectItemCaseSensitive(root,"readings");
    assert(cJSON_GetArraySize(readings)==2);
    assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(readings,0),"rssi_dbm")->valueint==-52);
    assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(readings,1),"rssi_dbm")->valueint==-58);
    cJSON_Delete(root);cJSON_free(body);
    body=clearchain_device_http_scan_json(&state,NULL,0,1); assert(body!=NULL); cJSON_free(body);
    assert(clearchain_device_http_scan_json(&state,NULL,0,0)==NULL);
    state.armed=0; assert(clearchain_device_http_scan_json(&state,samples,1,0)==NULL); state.armed=1;
    state.batch_id[0]='\0';assert(clearchain_device_http_scan_json(&state,samples,1,0)==NULL);
    state.mode=CLEARCHAIN_DEVICE_MODE_CP;
    body=clearchain_device_http_scan_json(&state,samples,1,0); assert(body);
    root=cJSON_Parse(body); assert(!cJSON_GetObjectItemCaseSensitive(root,"batch_id"));cJSON_Delete(root);cJSON_free(body);
    assert(clearchain_device_http_get_state(&state)<0);
    assert(network_calls==0);
    puts("PASS: API v2 armed gate, repeated signed RSSI, empty final, CP without batch and upload gate");
    return 0;
}
