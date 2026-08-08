#include "clearchain_demo_policy.h"

#include <string.h>

#include "osal_debug.h"

#define CLEARCHAIN_STAGE_FACTORY    1
#define CLEARCHAIN_STAGE_FDA        2
#define CLEARCHAIN_STAGE_WAREHOUSE  3
#define CLEARCHAIN_STAGE_CHECKPOINT 4

#define CLEARCHAIN_STAGE_BIT(stage) (1u << ((stage) - 1))

typedef struct {
    const char *epc;
    const char *name;
    uint8_t seen_stages;
} clearchain_demo_card_state_t;

static clearchain_demo_card_state_t g_demo_cards[] = {
    { CARD_A_EPC, "CARD_A normal full-chain drug", 0 },
    { CARD_B_EPC, "CARD_B uncertified drug", 0 },
    { CARD_C_EPC, "CARD_C chain-gap drug", 0 },
};

static clearchain_demo_card_state_t *clearchain_demo_find_card(const char *chip_uid)
{
    unsigned int i;

    if (chip_uid == NULL) {
        return NULL;
    }

    for (i = 0; i < sizeof(g_demo_cards) / sizeof(g_demo_cards[0]); i++) {
        if (strcmp(chip_uid, g_demo_cards[i].epc) == 0) {
            return &g_demo_cards[i];
        }
    }

    return NULL;
}

static int clearchain_demo_card_a_result(uint8_t seen_stages)
{
    uint8_t required_stages =
        CLEARCHAIN_STAGE_BIT(CLEARCHAIN_STAGE_FACTORY) |
        CLEARCHAIN_STAGE_BIT(CLEARCHAIN_STAGE_FDA) |
        CLEARCHAIN_STAGE_BIT(CLEARCHAIN_STAGE_WAREHOUSE);

    if ((seen_stages & required_stages) == required_stages) {
        osal_printk("Demo policy: CARD_A full chain complete, APPROVED\r\n");
        return CLEARCHAIN_SCAN_LED_GREEN;
    }

    osal_printk("Demo policy: CARD_A chain incomplete, INSPECTION REQUIRED\r\n");
    return CLEARCHAIN_SCAN_LED_RED;
}

int clearchain_demo_policy_apply(const char *chip_uid,
                                 const clearchain_stage_config_t *stage_config)
{
    clearchain_demo_card_state_t *card = clearchain_demo_find_card(chip_uid);

    if (card == NULL || stage_config == NULL ||
        stage_config->stage < CLEARCHAIN_STAGE_FACTORY ||
        stage_config->stage > CLEARCHAIN_STAGE_CHECKPOINT) {
        return CLEARCHAIN_SCAN_LED_UNKNOWN;
    }

    card->seen_stages |= CLEARCHAIN_STAGE_BIT(stage_config->stage);

    osal_printk("Demo policy: %s scanned stage %u, seen mask=0x%02x\r\n",
                card->name,
                stage_config->stage,
                card->seen_stages);

    if (stage_config->stage != CLEARCHAIN_STAGE_CHECKPOINT) {
        osal_printk("Demo policy: intermediate stage recorded, VERIFY\r\n");
        return CLEARCHAIN_SCAN_LED_ORANGE;
    }

    if (strcmp(card->epc, CARD_A_EPC) == 0) {
        int result = clearchain_demo_card_a_result(card->seen_stages);
        card->seen_stages = 0;
        return result;
    }

    if (strcmp(card->epc, CARD_B_EPC) == 0) {
        osal_printk("Demo policy: CARD_B uncertified drug, INSPECTION REQUIRED\r\n");
        card->seen_stages = 0;
        return CLEARCHAIN_SCAN_LED_RED;
    }

    if (strcmp(card->epc, CARD_C_EPC) == 0) {
        osal_printk("Demo policy: CARD_C configured as warehouse gap, INSPECTION REQUIRED\r\n");
        card->seen_stages = 0;
        return CLEARCHAIN_SCAN_LED_RED;
    }

    return CLEARCHAIN_SCAN_LED_UNKNOWN;
}
