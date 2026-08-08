#ifndef CLEARCHAIN_DEMO_POLICY_H
#define CLEARCHAIN_DEMO_POLICY_H

#include <stdint.h>

#include "clearchain_http.h"
#include "clearchain_key.h"

/*
 * Records local stage history for the three fixed demo cards.
 * Returns CLEARCHAIN_SCAN_LED_UNKNOWN when the hardware should use the backend
 * result instead of a local demo decision.
 */
int clearchain_demo_policy_apply(const char *chip_uid,
                                 const clearchain_stage_config_t *stage_config);

#endif
