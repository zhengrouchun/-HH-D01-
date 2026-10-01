/*
Copyright (C) 2024 HiHope Open Source Organization .
Licensed under the Apache License, Version 2.0
*/
// 这份代码的版权
// +
// 使用的开源许可证
// 版权所有 (C) 2024 HiHope 开源组织。
// 基于 Apache 2.0 许可证授权。

#include "string.h"//引入了字符串相关功能,strcmp,strncpy

#include "soc_osal.h"
#include "app_init.h"
#include "osal_debug.h"


#include "my_wifi_api.h"
#include "my_wifi_tcp.h"

#include "clearchain_config.h"
#include "clearchain_feedback.h"
#include "clearchain_http.h"
#include "clearchain_tca9555.h"
#include "clearchain_key.h"
#include "clearchain_display_link.h"

#include "r200_reader.h"



#define WIFI_TCP_CLIENT_TASK_PRIO 24

#define WIFI_TCP_CLIENT_TASK_STACK_SIZE 0x2000
#define TAG_MISSING_RESET_ROUNDS 10
#define RFID_POLL_INTERVAL_MS 500
#define CLEARCHAIN_S1_BATCH_WINDOW_MS CLEARCHAIN_BATCH_WINDOW_MS

static r200_batch_t g_factory_batch;

static void clearchain_batch_progress(const r200_batch_t *batch, uint32_t elapsed, uint32_t window)
{
    /* Window-time progress only; it never claims that all expected EPCs were read. */
    uint8_t percent = (uint8_t)(((uint64_t)elapsed * 100U) / window);
    (void)clearchain_display_scan_update(1U, percent, (uint8_t)batch->tag_count,
                                        (uint16_t)batch->total_samples);
}

static int clearchain_capture_factory_batch(void)
{
    clearchain_key_set_scan_busy(1);
    (void)clearchain_display_scan_started(1U);
    int ret = r200_reader_read_batch_progress(&g_factory_batch, CLEARCHAIN_S1_BATCH_WINDOW_MS,
                                              clearchain_batch_progress);
    if (ret != 0 || g_factory_batch.tag_count == 0U) {
        uint16_t error = ret == 0 ? CLEARCHAIN_ERROR_NO_TAGS :
            (g_factory_batch.capacity_drops ? CLEARCHAIN_ERROR_CAPACITY : CLEARCHAIN_ERROR_READER);
        osal_printk("S1 batch scan failed or incomplete; partial data retained in RAM\r\n");
        (void)clearchain_display_show_error(1U,error);
        clearchain_feedback_post_failed();
    } else {
        osal_printk("S1 batch captured locally: tags=%u samples=%u\r\n",
                    (unsigned int)g_factory_batch.tag_count, (unsigned int)g_factory_batch.total_samples);
        osal_printk("S1 upload paused: batch_id source and backend target need confirmation\r\n");
        (void)clearchain_display_scan_complete(1U,(uint8_t)g_factory_batch.tag_count,
                                              (uint16_t)g_factory_batch.total_samples);
    }
    clearchain_key_set_scan_busy(0);
    return ret;
}

static void *clearchain_wifi_task(void *arg)
{
    (void)arg;
    errcode_t ret=wifi_connectTo_AP(WIFI_SSID_NAME,WIFI_SSID_KEY);
    osal_printk("[CLEAR WIFI] connect returned 0x%x; local scan runs independently\r\n",ret);
    return NULL;
}

static void clearchain_start_wifi(void)
{
    osal_task *task=osal_kthread_create((osal_kthread_handler)clearchain_wifi_task,NULL,"CCWifi",0x2000);
    if (task==NULL) { osal_printk("[CLEAR WIFI] task failed; local scan continues\r\n"); return; }
    (void)osal_kthread_set_priority(task,26); osal_kfree(task);
}

static void clearchain_consume_ui_keys(void)
{
    clearchain_key_event_t event;
    clearchain_key_availability_t availability;
    while (clearchain_key_take_event(&event,&availability)) {
        uint8_t stage=clearchain_key_get_stage();
        if (event==CLEARCHAIN_KEY_D4_BACK) {
            (void)clearchain_display_show_waiting(stage);
        } else {
            /* No invented History/Image/CP backend requests or menu semantics. */
            (void)clearchain_display_show_error(stage,availability==CLEARCHAIN_KEY_DISABLED ?
                CLEARCHAIN_ERROR_DISABLED_KEY : CLEARCHAIN_ERROR_NOT_AVAILABLE);
        }
    }
}

void wifi_tcp_client_demo(void *param)
{
    errcode_t tca9555_ret;
/*这句话非常重要。
拆开看：errcode_t是数据类型。tca9555_ret是变量名。
你可以把它理解成：创建一个盒子,名字叫 tca9555_ret
专门保存 TCA9555 检测结果
为什么叫 ret？
因为：ret = return
程序员经常用 ret 表示：函数返回值,
所以：tca9555_ret基本就可以翻译成：TCA9555 的返回结果。
例如后面：tca9555_ret = clearchain_tca9555_probe();
如果检测成功：tca9555_ret = ERRCODE_SUCC
如果失败 tca9555_ret = 某个错误代码*/
    param = param;
/*void *param表示：
这个函数可以接收一个通用指针参数。但是你当前程序实际上没有使用这个参数。
所以紧接着：param = param; 
这句没有真正改变任何东西。
它相当于：param=param 自己,主要目的通常是避免编译器警告：
“你声明了 param，但是没有使用它。”*/
    osal_printk(
        "\r\n===== TcpClientDemoTask start =====\r\n"
        /*在串口终端打印一行文字。\r\n是换行,\r = 回到这一行开头
        \n = 换到下一行*/
    );

    tca9555_ret = clearchain_tca9555_probe();
    //检测结果被保存进：tca9555_ret
    if (tca9555_ret != ERRCODE_SUCC)
    //如果检测失败
    {
        osal_printk("Stop here: GPIO13/GPIO14 software I2C test failed, ret=0x%x\r\n", tca9555_ret);
        (void)clearchain_display_show_error(1U,CLEARCHAIN_ERROR_TCA);
        //%x表示：用十六进制方式显示一个整数。
        while (1) 
       //1表示永远成立 ，任务每次睡一秒，然后再次循环，但永远不会离开这个循环。防止继续运行后面的 RFID / Wi-Fi / HTTP 等业务
        {
            osal_msleep(1000);
        }
    }

    /*
     * 连接WiFi
     */

    clearchain_feedback_init();
//初始化你这个 ClearChain 项目里的“反馈设备”。这里的 feedback 很可能就是给用户反馈结果的：LED 灯,蜂鸣器
    /*
     *如果要准确知道 clearchain_feedback_init() 到底初始化了哪些 GPIO、LED、蜂鸣器，
     就要打开这个函数的定义继续看。
     */
    osal_printk(
        "Start wifi connect...\r\n"
    );

    clearchain_start_wifi();
/*让 WS63 使用指定的 Wi-Fi 名称和密码去连接无线路由器。
Wi-Fi 连接到 AP,AP即Access Point，叫做无线接入点。
*/
    osal_printk(
        "wifi connect function return\r\n"
    );
/*return 在这里的意思不是：Wi-Fi 一定连接成功。而是：
wifi_connectTo_AP() 这个函数执行结束，程序已经从这个函数里面返回了。*/
    /* Wi-Fi discovery is independent; do not delay local RFID startup. */



    /*
     * 初始化LED和蜂鸣器反馈
     */

    clearchain_feedback_init();
//初始化 LED / 蜂鸣器等反馈模块
    /*
     * 初始化R200
     */

    osal_printk(
        "Start R200 init...\r\n"
    );


    if (r200_reader_init() != 0) {
        (void)clearchain_display_show_error(1U,CLEARCHAIN_ERROR_READER);
        osal_printk("R200 init failed; check UART1 before continuing\r\n");
        return;
    }


    osal_printk(
        "R200 init done\r\n"
    );

    clearchain_key_start();
    (void)clearchain_display_show_waiting(clearchain_key_get_stage());
    uint32_t handled_stage_selection = clearchain_key_get_stage_selection_epoch();

    /*
     * 保存上一次扫描到的chip_uid
     *
     * 用于防止同一个标签重复发送
     */

    char last_chip_uid[R200_TAG_ID_MAX_LEN] = {0};
    //E28011704000021D35AFADD9你的 RFID EPC是字符类型。
    //保存“上一次已经处理过的 RFID 标签 EPC”。
    //[R200_TAG_ID_MAX_LEN]  它表示这个字符数组有多大。0相当于一开始它是一个空字符串：
    /*如果你错误地把：last_chip_uid也写到循环里面：
while(1)
{
    char last_chip_uid[...] = {0};
}
那每次循环都会清零：
第1轮：清零
第2轮：又清零
第3轮：又清零
它就永远记不住“上一次 EPC”，防重复功能基本就失效了。*/
    char present_chip_uid[R200_TAG_ID_MAX_LEN] = {0};
    //当前认为还放在 RFID 读卡器前面的标签
    uint8_t last_scan_stage = 0;
    //记录上一次 RFID 扫描发生在哪个阶段。（1 Factory
//2 FDA
//3 Warehouse
//4 Checkpoint
//5 Hospital）
    int missing_tag_rounds = 0;
    //标签没有读到的轮数/ 次数
    int wait_tag_removed = 0;
    //当前程序是不是正在等待用户把 RFID 标签拿走,是的话打印提示要移走标签
//1表示同一张卡还没拿走。
    while(1)//WS63 设备不是扫描一次 RFID 就结束,只要设备不断电,就一直扫描 RFID
    {
        clearchain_consume_ui_keys();
        uint32_t stage_selection = clearchain_key_get_stage_selection_epoch();
        if (clearchain_key_get_mode() == CLEARCHAIN_MODE_STAGE_1) {
            /* S1 is an explicit whole-batch action. It runs once per physical
             * S1 key press and never falls through to the per-tag /scan path. */
            if (stage_selection != handled_stage_selection) {
                handled_stage_selection = stage_selection;
                (void)clearchain_capture_factory_batch();
                handled_stage_selection = clearchain_key_get_stage_selection_epoch();
            }
            osal_msleep(RFID_POLL_INTERVAL_MS);
            continue;
        }
        handled_stage_selection = stage_selection;
        /*
         * 当前读取到的RFID EPC
         *
         * 实际内容:
         * E28011704000021D35AFADD9
         */
        char chip_uid[R200_TAG_ID_MAX_LEN] = {0};
//保存“这一轮扫描”刚刚从 R200 读取出来的 EPC。
        /*
         * 读取R200 EPC
         */

        if(r200_reader_read_epc(
                chip_uid,
                sizeof(chip_uid)
            ) == 0)
            //让 R200 读一次 RFID 标签，如果读到了 EPC，就把 EPC 放进 chip_uid 这个数组里。
        //返回 0=成功读取 EPC
            {
            if (clearchain_key_get_mode() == CLEARCHAIN_MODE_STAGE_1) {
                /* The stage may have changed while the blocking R200 read was
                 * in progress. Let the next loop run the S1 batch workflow. */
                continue;
            }
            missing_tag_rounds = 0;
//连续多少轮没有读到 RFID 标签。
            osal_printk(
                "Read CHIP_UID:%s\r\n",
                chip_uid
            );
//把刚刚读到的 EPC 打印到串口。
            /*
             * RFID读取反馈
             */

            /*
             * 判断是否为新标签
             */

            {
                const clearchain_stage_config_t *stage_config = clearchain_key_get_stage_config();
//当前选择的是哪个业务阶段。
            if(wait_tag_removed &&
               strcmp(chip_uid, present_chip_uid) == 0)
            // 同一张卡还没拿走
            // 不再发送 HTTP
            {
                osal_printk(
                    "Tag still present, remove before next scan, CHIP_UID:%s\r\n",
                    chip_uid
                );
            }
            else
            {

            if(strcmp( chip_uid,last_chip_uid) != 0 ||
                stage_config->stage != last_scan_stage)
                //现在的标签和前一张标签及所处的阶段不同，
            {
                clearchain_feedback_tag_read();
//已经确认这是一次有效的新扫描，所以调用反馈模块，告诉设备“读到标签了”。
//在 clearchain_feedback.c 里面对应实现
                strncpy(
                    present_chip_uid,
                    chip_uid,
                    sizeof(present_chip_uid)-1
                );
//C 字符串最后必须有\0结尾，所以这里用 sizeof(present_chip_uid)-1 来确保不会越界。
                present_chip_uid[
                    sizeof(present_chip_uid)-1
                ] = '\0';
//安全复制字符串，并确保最后一定有字符串结束符。
                wait_tag_removed = 1;
/*原来：wait_tag_removed = 0  表示：可以扫描。
第一次有效扫描完成以后：wait_tag_removed = 1
表示:这张卡已经处理过，现在必须先拿走。*/
                osal_printk(
                    "New scan send HTTP, CHIP_UID:%s, stage:%u (%s)\r\n",
                    chip_uid,
                    stage_config->stage,
                    stage_config->name
                );
//stage = 4     name = Checkpoint
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

                int scan_led;//保存结果的变量。为什么是 int？因为你的程序并不是直接把 "GREEN"、"ORANGE"、"RED" 这些文字存进去，
               // 而是用整数表示不同状态。
/*typedef enum {
    CLEARCHAIN_SCAN_LED_GREEN = 0,
    CLEARCHAIN_SCAN_LED_ORANGE = 1,
    CLEARCHAIN_SCAN_LED_RED = 2,
    CLEARCHAIN_SCAN_LED_UNKNOWN = 3
} clearchain_scan_led_t;*/
                {
                    /* S2-S5 remain the backend-defined one EPC -> /scan loop.
                     * /scan accumulates a pass and folds batch verification
                     * into the returned tag response. */
                    if (!CLEARCHAIN_UPLOAD_ALLOWED) {
                        osal_printk("[CLEAR HTTP] upload disabled: tag captured locally\r\n");
                        (void)clearchain_display_scan_complete(stage_config->stage,1U,0U);
                        scan_led = CLEARCHAIN_SCAN_LED_UNKNOWN;
                    } else {
                        scan_led = clearchain_send_scan(chip_uid);
                    }
                    clearchain_display_result_t display_result = CLEARCHAIN_DISPLAY_RESULT_UNKNOWN;
                    if (scan_led == CLEARCHAIN_SCAN_LED_GREEN) {
                        display_result = CLEARCHAIN_DISPLAY_RESULT_APPROVED;
                    } else if (scan_led == CLEARCHAIN_SCAN_LED_ORANGE) {
                        display_result = CLEARCHAIN_DISPLAY_RESULT_MONITOR;
                    } else if (scan_led == CLEARCHAIN_SCAN_LED_RED) {
                        display_result = CLEARCHAIN_DISPLAY_RESULT_REJECT;
                    }
                    if (CLEARCHAIN_UPLOAD_ALLOWED) {
                        (void)clearchain_display_show_result(display_result, CLEARCHAIN_RISK_SCORE_UNKNOWN);
                    }
//调用 clearchain_send_scan() 函数，把当前 RFID 标签的 EPC 也就是 chip_uid，交给它。
                    if (!CLEARCHAIN_UPLOAD_ALLOWED) {
                        /* Local acquisition has no backend verdict; no success/failure alarm. */
                    }
                    else if(scan_led == CLEARCHAIN_SCAN_LED_GREEN)
                    {
                        clearchain_feedback_post_success();//执行成功反馈。
                    }
                    else if(scan_led == CLEARCHAIN_SCAN_LED_ORANGE)
                    {
                        clearchain_feedback_verify();//需要进一步核验
/*关灯
↓
亮黄灯
↓
蜂鸣器响 250ms
↓
等待
↓
恢复待机*/
                    }
                    else if(scan_led == CLEARCHAIN_SCAN_LED_RED)
                    {
                        clearchain_feedback_risk_alert();//执行风险报警
/*关闭所有灯
↓
亮红灯
↓
蜂鸣器响 800ms
↓
保持一段时间
↓
恢复待机*/
                    }
                    else
                    {
                        clearchain_feedback_post_failed();
/*亮红灯
+
蜂鸣器响 300ms
+
等待
+
恢复待机*/
                    }
                }
/*scan_led = clearchain_send_scan(chip_uid)
              ↓
        服务器返回结果
              ↓
       scan_led 是什么？

        /      |      \
      GREEN  ORANGE   RED
       ↓       ↓       ↓
      绿灯    黄灯    红灯
      通过    核验    风险
如果都不是
       ↓
通信失败
       ↓
post_failed()*/
                /*
                 * 保存当前标签
                 */

                if(scan_led >= 0)
                /*你的 clearchain_send_scan() 成功时返回的是：

GREEN  = 0
ORANGE = 1
RED    = 2

失败时返回 -1。*/
                {
                    strncpy(
                        last_chip_uid,
                        chip_uid,
                        sizeof(last_chip_uid)-1
                    );
//把这次扫描到的 EPC 保存到 last_chip_uid。

                    last_chip_uid[
                        sizeof(last_chip_uid)-1
                    ] = '\0';
//保证字符串最后一定有结束符 \0，防止字符串越界或后面 strcmp() 出问题。
                    last_scan_stage = stage_config->stage;
                }
//把这次扫描发生的阶段也保存下来。
/*例如：

当前阶段 = Checkpoint = 4

保存后：

last_scan_stage = 4

为什么一定要这样设计？

因为后面程序要判断：

strcmp(chip_uid, last_chip_uid) != 0
||
stage_config->stage != last_scan_stage
也就是：
卡变了
或者
阶段变了
只要其中一个变了，就允许重新发送。
所以这里必须同时记住两样东西：
last_chip_uid
→ 上一次是哪张卡
last_scan_stage
→ 上一次在哪个阶段*/

            }
            else
            {
                osal_printk(
                    "Duplicate tag ignored, CHIP_UID:%s, stage:%u (%s)\r\n",
                    chip_uid,
                    stage_config->stage,
                    stage_config->name
                );
                //如果标签没变，并且阶段也没变，那么这是重复扫描，不再发送 HTTP，只打印提示“检测到重复标签，已经忽略”。。
            }
            }
            }
        }

        else
        {
            /*
             * 没有读取到标签,这张卡已经离开读卡区域。
             */
            clearchain_feedback_standby();
            //当前没读到卡，让 LED / 蜂鸣器恢复待机状态。
            if (wait_tag_removed) {
                /*之前成功扫描一张卡
↓
wait_tag_removed = 1
↓
程序进入“等待卡离开”状态

下一轮 R200 扫描
↓
如果还能读到这张卡
↓
说明它还在
↓
继续等待

如果 R200 读不到标签
↓
进入 else
↓
此时又发现 wait_tag_removed == 1,即有扫过卡片但现在检测不到这个卡片了
↓
代码推断：
“刚才那张卡应该已经离开了”
↓
打印 Tag removed
↓
wait_tag_removed = 0*/
                osal_printk("Tag removed, ready for next stage\r\n");
                wait_tag_removed = 0;
                (void)clearchain_display_show_waiting(clearchain_key_get_stage());
                //不再等待拿卡，现在可以接受下一次扫描。
                present_chip_uid[0] = '\0';
/*意思是：把 present_chip_uid 这个字符串的第 1 个字符改成字符串结束符 '\0'。
假设原来：present_chip_uid = "E28011704000021D35AFADD9";
执行后，C 语言再把它当字符串读取时，会认为它是：
""也就是空字符串。
注意：这不是把整个数组每个字节都清零，而是把第一个字符设成 '\0'，从字符串角度看就等于“清空了”。*/
                last_chip_uid[0] = '\0';
//同理，表示清除“上一次成功扫描的 RFID 标签”。
                last_scan_stage = 0;
//
                missing_tag_rounds = 0;
//连续多少轮没有读到标签的计数器清除
            }
            if (last_chip_uid[0] != '\0') 
            //last_chip_uid 不是空字符串，也就是程序还记着“上一次扫描的标签”
            {
                missing_tag_rounds++;//“连续没读到标签”的次数 +1
                if (missing_tag_rounds >= TAG_MISSING_RESET_ROUNDS) 
                //相当于：if (missing_tag_rounds >= 10)意思是：
//如果已经连续 10 轮都没读到标签，就认为旧标签记录没有必要继续保留了
                {
                    osal_printk("Tag missing, clear last CHIP_UID:%s\r\n", last_chip_uid);
                    last_chip_uid[0] = '\0';
                    last_scan_stage = 0;
                    missing_tag_rounds = 0;
                }
            }
        }
        /*
         * R200扫描间隔
         */

        osal_msleep(RFID_POLL_INTERVAL_MS);
/*如果不休眠：
while(1)
{
    一直疯狂扫描
}
CPU 会不停跑，R200 也会被高频调用。
加上osal_msleep(500); 
以后变成：
扫描一次
↓
等 500ms
↓
再扫描一次
↓
再等 500ms

这样可以降低 CPU 占用，也避免过于频繁地轮询 RFID。*/
    }

}
/*创建一个新的 LiteOS 任务，让 wifi_tcp_client_demo() 在独立线程里长期运行，
然后给这个任务设置优先级。*/
static void tcp_client_demo_entry(void)
/*定义一个函数：
函数名：tcp_client_demo_entry
返回值：void，不返回数据
参数：void，没有参数*/
/*
tcp_client_demo_entry()
自己不负责扫描 RFID。
它只是负责：创建一个任务，让 wifi_tcp_client_demo() 去真正干活。*/
{
    osal_task *task_handle = NULL;
    if (clearchain_display_link_init() != 0) {
        osal_printk("[CLEAR SLE] display link init failed; ClearChain continues\r\n");
    }
/*定义一个任务句柄：
task_handle
↓
用来保存“新创建任务”的引用*/
    osal_kthread_lock();
    /*暂时锁住内核线程调度相关操作。
    因为接下来要连续完成：
创建任务
↓
设置任务优先级
↓
处理任务句柄
代码希望这一小段操作完整地执行完，再恢复正常调度。
    */
   //创建一个新的线程/任务。
    task_handle = osal_kthread_create(
        (osal_kthread_handler)wifi_tcp_client_demo,
        0,
        "TcpClientDemoTask",
        WIFI_TCP_CLIENT_TASK_STACK_SIZE
    );
    /*
    1.新任务启动以后，要执行哪个函数
    2.0表示对应传给：wifi_tcp_client_demo(void *param)里的：param=0
    3.任务名字。主要方便调试、日志、查看任务状态。
    可以理解成：给这个线程起名叫 TcpClientDemoTask。
    4.任务栈大小。单位是字节。0x2000 = 8192 字节 = 8KB
    */
    if(task_handle != NULL)//如果任务创建成功
    {
        osal_kthread_set_priority(
            task_handle,
            WIFI_TCP_CLIENT_TASK_PRIO
        );
        /*
        前面定义：#define WIFI_TCP_CLIENT_TASK_PRIO 24
        把 TcpClientDemoTask 的优先级设置为 24
        */
        osal_kfree(task_handle);
        //在任务创建和配置完成后释放这个返回句柄对应的内存。
    }
    osal_kthread_unlock();
//任务创建配置完成，现在恢复正常的线程调度。和前面的：osal_kthread_lock();对应。
}

app_run(tcp_client_demo_entry);
//把 tcp_client_demo_entry 注册成这个应用的启动入口，让系统启动到应用层时自动执行它。
/*
app_run(tcp_client_demo_entry)
        ↓
系统启动应用
        ↓
tcp_client_demo_entry()
        ↓
osal_kthread_create(...)
        ↓
创建 TcpClientDemoTask
        ↓
wifi_tcp_client_demo()
        ↓
连接 Wi-Fi
初始化 R200
循环扫描 RFID
POST /scan
控制 LED / 蜂鸣器*/
/*
app_run(tcp_client_demo_entry);
不是在扫描 RFID。
它只是：注册启动函数。
真正创建任务的是：osal_kthread_create(...)
真正执行 RFID + Wi-Fi + HTTP 主业务的是：wifi_tcp_client_demo()
所以三者关系可以记成：
app_run()
= 注册入口
tcp_client_demo_entry()
= 创建任务
wifi_tcp_client_demo()
= 真正干活
*/
