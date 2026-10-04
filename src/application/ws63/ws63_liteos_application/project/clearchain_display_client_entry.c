#include "app_init.h"
#include "clearchain_display_client.h"
#include "soc_osal.h"

static void clearchain_display_client_entry(void)
{
    /* FIX: was 0x1800 (6 KB). LVGL display-create + first render (flush_cb ->
     * lv_draw_sw_rgb565_swap -> SPI poll loop) overflowed the 6/8 KB stack and
     * clearchain_ui_init() returned -1 -> permanent white screen. 16 KB proved
     * enough on the lcd_test bench; we give 20 KB here for SLE-callback headroom. */
    osal_task *task = osal_kthread_create((osal_kthread_handler)clearchain_display_client_run,
                                          NULL, "CCDisplayClient", 0x5000);
    if (task == NULL) {
        osal_printk("[CLEAR SLE] client task creation failed\r\n");
        return;
    }
    /* FIX #2 (2026-10-04) -- DO NOT call osal_kthread_set_priority() here.
     *
     * Root cause of the remaining white screen / reboot loop (NOT stack):
     *   osal_kthread_set_priority(task, 28)
     *     -> LOS_TaskPriSet()                 (los_task.c)
     *       -> LOS_Schedule()                 (fires immediately: no sched lock)
     *         -> OsSchedPreempt()             (los_sched.c:123)
     *           -> OsPriQueueEnqueue(&runTask->pendList, runTask->priority, ...)
     *              with priority == 32 (OUT OF RANGE; g_priQueueList[] has only
     *              32 slots, valid 0..31 / LOS_TASK_PRIORITY_LOWEST == 31)
     *           -> g_priQueueList[32] reads past the array; its pstPrev is NULL
     *           -> instruction "lw a4,4(a5)" loads from address 0x4
     *           -> Load access fault: mcause=0x5, mtval=0x4, mepc=0x24cf7a
     *              (= OsPriQueueEnqueue, caller ra=0xa0d68e = OsSchedPreempt).
     *
     * clearchain_lcd_selftest.c (the working LCD bench) hits the same fault and
     * avoids it by NOT touching the priority during app init -- see the comment
     * in lcd_test_entry(). The task created above already runs at the default
     * OSAL priority (LOSCFG_BASE_CORE_TSK_DEFAULT_PRIO == 10), which is more
     * than enough for the 50 ms render loop in clearchain_display_client_run().
     */
    osal_printk("[CLEAR SLE] client task created (prio=default), entry done\r\n");
    osal_kfree(task);
}

app_run(clearchain_display_client_entry);
