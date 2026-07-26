#ifndef MY_WIFI_API_H
#define MY_WIFI_API_H

#include "errcode.h"

errcode_t wifi_connectTo_AP(const char *expected_ssid, const char *key);
errcode_t softap_start(void);
void softap_stop(void);

#endif