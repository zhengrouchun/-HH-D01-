#include "app_init.h"
#include "clearchain_display_client.h"
#include "soc_osal.h"

static void clearchain_display_client_entry(void)
{
    osal_task *task = osal_kthread_create((osal_kthread_handler)clearchain_display_client_run,
                                          NULL, "CCDisplayClient", 0x1800);
    if (task == NULL) {
        osal_printk("[CLEAR SLE] client task creation failed\r\n");
        return;
    }
    (void)osal_kthread_set_priority(task, 28);
    osal_kfree(task);
}

app_run(clearchain_display_client_entry);
