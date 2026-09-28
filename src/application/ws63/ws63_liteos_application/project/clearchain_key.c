#include "clearchain_key.h"

#include "clearchain_tca9555.h"
#include "clearchain_display_link.h"
#include "soc_osal.h"

/* Five buttons: TCA9555 P10-P14, each button to GND with a 10 kOhm pull-up to 3V3. */
#define CLEARCHAIN_KEY_PORT        CLEARCHAIN_TCA9555_PORT1
#define CLEARCHAIN_KEY_POLL_MS     20
#define CLEARCHAIN_KEY_DEBOUNCE_COUNT 2
#define CLEARCHAIN_STAGE_COUNT     5
#define CLEARCHAIN_EXTRA_KEY_COUNT 9
#define CLEARCHAIN_EVENT_QUEUE_SIZE 16

/*
 * The stage keys are expected to have pull-ups and short their TCA9555 input
 * to GND when pressed.  Keep this definition in one place: the boot and edge
 * logs below make an incorrect wiring/polarity obvious on the serial console.
 */
#define CLEARCHAIN_KEY_PRESSED_LEVEL CLEARCHAIN_TCA9555_LEVEL_LOW

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
static volatile uint8_t g_stage = 1;
static volatile clearchain_mode_t g_mode = CLEARCHAIN_MODE_STAGE_1;
static int g_key_started = 0;

typedef struct {
    clearchain_key_event_t event;
    clearchain_key_availability_t availability;
} clearchain_queued_key_t;

/* TCA9555 #2: P00-P04 navigation, P10-P13 D1-D4. Other pins stay reserved. */
static const uint8_t g_extra_ports[CLEARCHAIN_EXTRA_KEY_COUNT] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1
};
static const uint8_t g_extra_pins[CLEARCHAIN_EXTRA_KEY_COUNT] = {
    0, 1, 2, 3, 4, 0, 1, 2, 3
};
static uint8_t g_extra_last[CLEARCHAIN_EXTRA_KEY_COUNT];
static uint8_t g_extra_stable[CLEARCHAIN_EXTRA_KEY_COUNT];
static uint8_t g_extra_same_count[CLEARCHAIN_EXTRA_KEY_COUNT];
static clearchain_queued_key_t g_events[CLEARCHAIN_EVENT_QUEUE_SIZE];
static uint8_t g_event_head;
static uint8_t g_event_tail;
static int g_extra_ready;

clearchain_key_availability_t clearchain_key_availability(clearchain_key_event_t event,
                                                           clearchain_mode_t mode)
{
    if (mode < CLEARCHAIN_MODE_STAGE_1 || mode > CLEARCHAIN_MODE_CP ||
        event < CLEARCHAIN_KEY_UP || event > CLEARCHAIN_KEY_D4_BACK) {
        return CLEARCHAIN_KEY_DISABLED;
    }
    if (event == CLEARCHAIN_KEY_D2_VIEW_ORIGINAL) {
        return mode == CLEARCHAIN_MODE_CP ? CLEARCHAIN_KEY_ENABLED : CLEARCHAIN_KEY_DISABLED;
    }
    if (event == CLEARCHAIN_KEY_D3_VIEW_IMAGE) {
        return (mode == CLEARCHAIN_MODE_STAGE_4 || mode == CLEARCHAIN_MODE_STAGE_5 ||
                mode == CLEARCHAIN_MODE_CP) ? CLEARCHAIN_KEY_ENABLED : CLEARCHAIN_KEY_DISABLED;
    }
    return CLEARCHAIN_KEY_ENABLED;
}

static void clearchain_key_push_event(clearchain_key_event_t event)
{
    unsigned int irq_status = osal_irq_lock();
    uint8_t next = (uint8_t)((g_event_head + 1U) % CLEARCHAIN_EVENT_QUEUE_SIZE);
    if (next == g_event_tail) {
        /* Until a UI consumer exists, keep the newest physical key events. */
        g_event_tail = (uint8_t)((g_event_tail + 1U) % CLEARCHAIN_EVENT_QUEUE_SIZE);
    }
    g_events[g_event_head].event = event;
    g_events[g_event_head].availability = clearchain_key_availability(event, g_mode);
    g_event_head = next;
    osal_irq_restore(irq_status);
}

int clearchain_key_take_event(clearchain_key_event_t *event,
                              clearchain_key_availability_t *availability)
{
    unsigned int irq_status;
    if (event == NULL || availability == NULL) {
        return 0;
    }
    irq_status = osal_irq_lock();
    if (g_event_head == g_event_tail) {
        osal_irq_restore(irq_status);
        return 0;
    }
    *event = g_events[g_event_tail].event;
    *availability = g_events[g_event_tail].availability;
    g_event_tail = (uint8_t)((g_event_tail + 1U) % CLEARCHAIN_EVENT_QUEUE_SIZE);
    osal_irq_restore(irq_status);
    return 1;
}

static void clearchain_key_sync_initial_levels(void)
{
    for (uint8_t i = 0; i < CLEARCHAIN_STAGE_COUNT; i++) {
        uint8_t level;

        if (clearchain_tca9555_read_pin(CLEARCHAIN_KEY_PORT, g_key_pins[i], &level) == ERRCODE_SUCC) {
            g_last_level[i] = level;
            g_stable_level[i] = level;
            g_same_level_count[i] = 0;
            osal_printk("Stage key %u input P1%u initial level=%u (pressed level=%u)\r\n",
                        (uint8_t)(i + 1), g_key_pins[i], level,
                        CLEARCHAIN_KEY_PRESSED_LEVEL);
        } else {
            osal_printk("Stage key %u input P1%u read failed during init\r\n",
                        (uint8_t)(i + 1), g_key_pins[i]);
        }
    }
}

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
        osal_printk("Stage key %u input P1%u changed: %u -> %u\r\n",
                    (uint8_t)(key_index + 1), g_key_pins[key_index],
                    g_last_level[key_index], level);
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
        return level == CLEARCHAIN_KEY_PRESSED_LEVEL;
    }

    return 0;
}

static void clearchain_extra_keys_init(void)
{
    clearchain_tca9555_device_t *device = clearchain_tca9555_get_device(1U);
    if (device == NULL || clearchain_tca9555_device_probe(device) != ERRCODE_SUCC) {
        osal_printk("Extra keys unavailable: TCA9555 0x21 probe failed\r\n");
        return;
    }
    for (uint8_t i = 0; i < CLEARCHAIN_EXTRA_KEY_COUNT; i++) {
        uint8_t level;
        if (clearchain_tca9555_device_read_pin(device, g_extra_ports[i],
                                                g_extra_pins[i], &level) != ERRCODE_SUCC) {
            osal_printk("Extra key %u initial read failed\r\n", (unsigned int)i);
            return;
        }
        g_extra_last[i] = level;
        g_extra_stable[i] = level;
        g_extra_same_count[i] = 0U;
    }
    g_extra_ready = 1;
    osal_printk("Extra keys ready: TCA9555 0x21\r\n");
}

static void clearchain_extra_keys_poll(void)
{
    clearchain_tca9555_device_t *device = clearchain_tca9555_get_device(1U);
    if (!g_extra_ready || device == NULL) {
        return;
    }
    for (uint8_t i = 0; i < CLEARCHAIN_EXTRA_KEY_COUNT; i++) {
        uint8_t level;
        if (clearchain_tca9555_device_read_pin(device, g_extra_ports[i],
                                                g_extra_pins[i], &level) != ERRCODE_SUCC) {
            continue;
        }
        if (level != g_extra_last[i]) {
            g_extra_last[i] = level;
            g_extra_same_count[i] = 0U;
            continue;
        }
        if (g_extra_same_count[i] < CLEARCHAIN_KEY_DEBOUNCE_COUNT) {
            g_extra_same_count[i]++;
        }
        if (level != g_extra_stable[i] &&
            g_extra_same_count[i] >= CLEARCHAIN_KEY_DEBOUNCE_COUNT) {
            g_extra_stable[i] = level;
            if (level == CLEARCHAIN_KEY_PRESSED_LEVEL) {
                clearchain_key_event_t event = (clearchain_key_event_t)(CLEARCHAIN_KEY_UP + i);
                clearchain_key_push_event(event);
                osal_printk("Extra key %u pressed: mode=%u availability=%u\r\n",
                            (unsigned int)event, (unsigned int)g_mode,
                            (unsigned int)clearchain_key_availability(event, g_mode));
            }
        }
    }
}

static void clearchain_key_task(void *param)
{
    param = param;

    while (1) {
        for (uint8_t i = 0; i < CLEARCHAIN_STAGE_COUNT; i++) {
            if (clearchain_key_poll(i)) {
                g_stage = g_stage_configs[i].stage;
                g_mode = (clearchain_mode_t)(CLEARCHAIN_MODE_STAGE_1 + i);
                (void)clearchain_display_stage_changed(g_stage);
                osal_printk("Stage button %u pressed: selected stage %u (%s), scanner_id=%s, stage_code=%s\r\n",
                            (uint8_t)(i + 1),
                            g_stage,
                            g_stage_configs[i].name,
                            g_stage_configs[i].scanner_id,
                            g_stage_configs[i].stage_code);
            }
        }
        clearchain_extra_keys_poll();
        osal_msleep(CLEARCHAIN_KEY_POLL_MS);
    }
}

void clearchain_key_start(void)
{
    osal_task *task_handle;
    const clearchain_stage_config_t *stage_config = clearchain_key_get_stage_config();

    if (g_key_started) {
        return;
    }

    clearchain_key_sync_initial_levels();
    clearchain_extra_keys_init();

    osal_printk("Stage key default: selected stage %u (%s), scanner_id=%s, stage_code=%s\r\n",
                stage_config->stage,
                stage_config->name,
                stage_config->scanner_id,
                stage_config->stage_code);

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

clearchain_mode_t clearchain_key_get_mode(void)
{
    return g_mode;
}

const clearchain_stage_config_t *clearchain_key_get_stage_config(void)
{
    uint8_t stage = g_stage;

    if (stage < 1 || stage > CLEARCHAIN_STAGE_COUNT) {
        stage = 4;
    }

    return &g_stage_configs[stage - 1];
}
