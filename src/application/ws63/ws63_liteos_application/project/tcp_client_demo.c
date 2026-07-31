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


    osal_printk(
        "\r\n===== TcpClientDemoTask start =====\r\n"
    );




    /*
     * 连接WiFi
     */

    clearchain_feedback_init();

    osal_printk(
        "Start wifi connect...\r\n"
    );


    wifi_connectTo_AP(
        WIFI_SSID_NAME,
        WIFI_SSID_KEY
    );


    osal_printk(
        "wifi connect function return\r\n"
    );


    osal_msleep(5000);



    /*
     * 初始化LED和蜂鸣器反馈
     */

    clearchain_feedback_init();




    /*
     * 初始化R200
     */

    osal_printk(
        "Start R200 init...\r\n"
    );


    r200_reader_init();


    osal_printk(
        "R200 init done\r\n"
    );




    /*
     * 保存上一次扫描到的chip_uid
     *
     * 用于防止同一个标签重复发送
     */

    char last_chip_uid[R200_TAG_ID_MAX_LEN] = {0};




    while(1)
    {


        /*
         * 当前读取到的RFID EPC
         *
         * 实际内容:
         * E28011704000021D35AFADD9
         */

        char chip_uid[R200_TAG_ID_MAX_LEN] = {0};




        /*
         * 读取R200 EPC
         */

        if(r200_reader_read_epc(
                chip_uid,
                sizeof(chip_uid)
            ) == 0)
        {


            osal_printk(
                "Read CHIP_UID:%s\r\n",
                chip_uid
            );



            /*
             * RFID读取反馈
             */

            clearchain_feedback_tag_read();




            /*
             * 判断是否为新标签
             */

            if(strcmp(
                    chip_uid,
                    last_chip_uid
                ) != 0)
            {


                osal_printk(
                    "New CHIP_UID send HTTP:%s\r\n",
                    chip_uid
                );



                /*
                 * 发送POST /scan
                 *
                 * clearchain_http.c
                 *
                 * 会组装:
                 *
                 * {
                 *  "chip_uid":"",
                 *  "scanner_id":"",
                 *  "scan_type":1,
                 *  "stage_code":""
                 * }
                 *
                 */

                if(clearchain_send_scan(chip_uid) == 0)
                {

                    clearchain_feedback_post_success();

                }
                else
                {

                    clearchain_feedback_post_failed();

                }





                /*
                 * 保存当前标签
                 */

                strncpy(
                    last_chip_uid,
                    chip_uid,
                    sizeof(last_chip_uid)-1
                );


                last_chip_uid[
                    sizeof(last_chip_uid)-1
                ] = '\0';



            }


        }
        else
        {

            /*
             * 没有读取到标签
             */

            clearchain_feedback_standby();

        }



        /*
         * R200扫描间隔
         */

        osal_msleep(2000);

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
