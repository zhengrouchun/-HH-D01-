#include "clearchain_key.h"

#include "clearchain_tca9555.h"
#include "soc_osal.h"

/* Five buttons: TCA9555 P10-P14, each button to GND with a 10 kOhm pull-up to 3V3. */
#define CLEARCHAIN_KEY_PORT        CLEARCHAIN_TCA9555_PORT1
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

static const uint8_t g_key_pins[CLEARCHAIN_STAGE_COUNT] = { 0, 1, 2, 3, 4 };

static uint8_t g_last_level[CLEARCHAIN_STAGE_COUNT] = {
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
};
static uint8_t g_stable_level[CLEARCHAIN_STAGE_COUNT] = {
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
    CLEARCHAIN_TCA9555_LEVEL_HIGH,
};
static uint8_t g_same_level_count[CLEARCHAIN_STAGE_COUNT] = { 0 };
static uint8_t g_stage = 4;
static int g_key_started = 0;

static int clearchain_key_poll(uint8_t key_index)
{
    uint8_t level;

    if (key_index >= CLEARCHAIN_STAGE_COUNT) {
        return 0;
    }

    if (clearchain_tca9555_read_pin(CLEARCHAIN_KEY_PORT, g_key_pins[key_index], &level) != ERRCODE_SUCC) {
        return 0;
    }

    if (level != g_last_level[key_index]) {
        g_last_level[key_index] = level;
        g_same_level_count[key_index] = 0;
        return 0;
    }

    if (g_same_level_count[key_index] < CLEARCHAIN_KEY_DEBOUNCE_COUNT) {
        g_same_level_count[key_index]++;
    }

    if (level != g_stable_level[key_index] &&
        g_same_level_count[key_index] >= CLEARCHAIN_KEY_DEBOUNCE_COUNT) {
        g_stable_level[key_index] = level;
        return level == CLEARCHAIN_KEY_PRESSED;
    }

    return 0;
}

static void clearchain_key_task(void *param)
{
    param = param;

    while (1) {
        for (uint8_t i = 0; i < CLEARCHAIN_STAGE_COUNT; i++) {
            if (clearchain_key_poll(i)) {
                g_stage = g_stage_configs[i].stage;
                osal_printk("Stage button %u pressed: selected stage %u (%s), scanner_id=%s, stage_code=%s\r\n",
                            (uint8_t)(i + 1),
                            g_stage,
                            g_stage_configs[i].name,
                            g_stage_configs[i].scanner_id,
                            g_stage_configs[i].stage_code);
            }
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
