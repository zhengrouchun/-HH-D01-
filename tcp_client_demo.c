/*
Copyright (C) 2024 HiHope Open Source Organization .
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "string.h"

#include "soc_osal.h"
#include "app_init.h"
#include "osal_debug.h"

#include "my_wifi_api.h"
#include "my_wifi_tcp.h"

#include "clearchain_config.h"
#include "clearchain_http.h"
#include "rc522_reader.h"

#define WIFI_TCP_CLIENT_TASK_PRIO 24
#define WIFI_TCP_CLIENT_TASK_STACK_SIZE 0x2000

void wifi_tcp_client_demo(void *param)
{
    param = param;

    wifi_connectTo_AP(
        WIFI_SSID_NAME,
        WIFI_SSID_KEY
    );

    sleep(5);
    rc522_reader_init();

    char last_tag_id[RC522_TAG_ID_MAX_LEN] = {0};

    while(1)
    {
        char tag_id[RC522_TAG_ID_MAX_LEN] = {0};

        if(rc522_reader_read_tag(tag_id, sizeof(tag_id)) == 0)
        {
            if(strcmp(tag_id, last_tag_id) != 0)
            {
                clearchain_send_scan(
                    tag_id
                );

                strncpy(last_tag_id, tag_id, sizeof(last_tag_id) - 1);
                last_tag_id[sizeof(last_tag_id) - 1] = '\0';
            }
        }

        sleep(1);
    }
}

static void tcp_client_demo_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();

    task_handle = osal_kthread_create(
        (osal_kthread_handler)wifi_tcp_client_demo,
        0,
        "TcpClientDemoTask",
        WIFI_TCP_CLIENT_TASK_STACK_SIZE
    );

    if(task_handle != NULL)
    {
        osal_kthread_set_priority(
            task_handle,
            WIFI_TCP_CLIENT_TASK_PRIO
        );

        osal_kfree(task_handle);
    }

    osal_kthread_unlock();
}

app_run(tcp_client_demo_entry);
