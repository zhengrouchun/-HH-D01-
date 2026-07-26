/*
Copyright (C) 2024 HiHope Open Source Organization .
Licensed under the Apache License, Version 2.0
*/

#include "string.h"

#include "soc_osal.h"
#include "app_init.h"
#include "osal_debug.h"

#include "my_wifi_api.h"
#include "my_wifi_tcp.h"

#include "clearchain_config.h"
#include "clearchain_feedback.h"
#include "clearchain_http.h"
#include "r200_reader.h"


#define WIFI_TCP_CLIENT_TASK_PRIO 24
#define WIFI_TCP_CLIENT_TASK_STACK_SIZE 0x2000


void wifi_tcp_client_demo(void *param)
{
    param = param;

    osal_printk("\r\n===== TcpClientDemoTask start =====\r\n");


    /*
     * 连接WiFi
     */
    osal_printk("Start wifi connect...\r\n");

    wifi_connectTo_AP(
        WIFI_SSID_NAME,
        WIFI_SSID_KEY
    );


    osal_printk("wifi connect function return\r\n");


    osal_msleep(5000);


    /*
     * Init LED and buzzer feedback.
     */
    clearchain_feedback_init();


    /*
     * Init R200
     */
    osal_printk("Start R200 init...\r\n");


    r200_reader_init();


    osal_printk("R200 init done\r\n");


    char last_tag_id[R200_TAG_ID_MAX_LEN] = {0};


    while(1)
    {

        char tag_id[R200_TAG_ID_MAX_LEN] = {0};


        if(r200_reader_read_epc(tag_id,
                                sizeof(tag_id)) == 0)
        {

            osal_printk("Read TAG:%s\r\n",
                        tag_id);


            clearchain_feedback_tag_read();


            if(strcmp(tag_id,last_tag_id) != 0)
            {

                osal_printk("New TAG send HTTP:%s\r\n",
                            tag_id);


                if(clearchain_send_scan(tag_id) == 0)
                {
                    clearchain_feedback_post_success();
                }
                else
                {
                    clearchain_feedback_post_failed();
                }


                strncpy(last_tag_id,
                        tag_id,
                        sizeof(last_tag_id)-1);


                last_tag_id[
                    sizeof(last_tag_id)-1
                ] = '\0';

            }
        }
        else
        {
            clearchain_feedback_standby();
        }


       osal_msleep(1000);
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
