#include "clearchain_key.h"

#include "clearchain_tca9555.h"
#include "soc_osal.h"

/* One button: TCA9555 P10, button to GND, external 10 kOhm pull-up to 3V3. */
#define CLEARCHAIN_KEY_PORT        CLEARCHAIN_TCA9555_PORT1
#define CLEARCHAIN_KEY_PIN         0
#define CLEARCHAIN_KEY_PRESSED     CLEARCHAIN_TCA9555_LEVEL_LOW
#define CLEARCHAIN_KEY_POLL_MS     20
#define CLEARCHAIN_KEY_DEBOUNCE_COUNT 2
#define CLEARCHAIN_STAGE_COUNT     5

static const clearchain_stage_config_t g_stage_configs[CLEARCHAIN_STAGE_COUNT] = {
    { 1, "Factory", "scanner_factory", "PROD-7f2a" },
    { 2, "FDA", "scanner_fda", "FDA-91xq" },
    { 3, "Warehouse", "scanner_warehouse", "WARE-3kd8" },
    { 4, "Checkpoint", "scanner_checkpoint", "PUB-c72m" },
    { 5, "Hospital", "scanner_hospital", "PRIV-a9z1" },
};

static uint8_t g_last_level = CLEARCHAIN_TCA9555_LEVEL_HIGH;
static uint8_t g_stable_level = CLEARCHAIN_TCA9555_LEVEL_HIGH;
static uint8_t g_same_level_count = 0;
static uint8_t g_stage = 4;
static int g_key_started = 0;

static int clearchain_key_poll(void)
{
    uint8_t level;

    if (clearchain_tca9555_read_pin(CLEARCHAIN_KEY_PORT, CLEARCHAIN_KEY_PIN, &level) != ERRCODE_SUCC) {
        return 0;
    }

    if (level != g_last_level) {
        g_last_level = level;
        g_same_level_count = 0;
        return 0;
    }

    if (g_same_level_count < CLEARCHAIN_KEY_DEBOUNCE_COUNT) {
        g_same_level_count++;
    }

    if (level != g_stable_level && g_same_level_count >= CLEARCHAIN_KEY_DEBOUNCE_COUNT) {
        g_stable_level = level;
        return level == CLEARCHAIN_KEY_PRESSED;
    }

    return 0;
}

static void clearchain_key_task(void *param)
{
    param = param;

    while (1) {
        if (clearchain_key_poll()) {
            g_stage = (uint8_t)(g_stage % CLEARCHAIN_STAGE_COUNT + 1);
            osal_printk("Stage button pressed: selected stage %u (%s)\r\n",
                        g_stage, clearchain_key_get_stage_config()->name);
        }
        osal_msleep(CLEARCHAIN_KEY_POLL_MS);
    }
}

void clearchain_key_start(void)
{
    osal_task *task_handle;

    if (g_key_started) {
        return;
    }

    task_handle = osal_kthread_create((osal_kthread_handler)clearchain_key_task, 0, "ClearChainKey", 0x800);
    if (task_handle == NULL) {
        osal_printk("Stage key task create failed\r\n");
        return;
    }

    osal_kthread_set_priority(task_handle, 24);
    osal_kfree(task_handle);
    g_key_started = 1;
}

uint8_t clearchain_key_get_stage(void)
{
    return g_stage;
}

const clearchain_stage_config_t *clearchain_key_get_stage_config(void)
{
    uint8_t stage = g_stage;

    if (stage < 1 || stage > CLEARCHAIN_STAGE_COUNT) {
        stage = 4;
    }

    return &g_stage_configs[stage - 1];
}
