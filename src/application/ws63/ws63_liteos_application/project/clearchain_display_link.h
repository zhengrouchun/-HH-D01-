#ifndef CLEARCHAIN_DISPLAY_LINK_H
#define CLEARCHAIN_DISPLAY_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "clearchain_display_protocol.h"

#if defined(CONFIG_CLEARCHAIN_DISPLAY_SLE_SERVER)
int clearchain_display_link_init(void);
bool clearchain_display_is_connected(void);
int clearchain_display_show_waiting(uint8_t stage);
int clearchain_display_stage_changed(uint8_t stage);
/* TODO: waiting for a measured/contracted real progress source. */
int clearchain_display_show_progress(uint8_t stage, uint8_t percent);
int clearchain_display_show_result(clearchain_display_result_t result, uint8_t risk_score);
#else
static inline int clearchain_display_link_init(void) { return 0; }
static inline bool clearchain_display_is_connected(void) { return false; }
static inline int clearchain_display_show_waiting(uint8_t stage) { (void)stage; return 0; }
static inline int clearchain_display_stage_changed(uint8_t stage) { (void)stage; return 0; }
static inline int clearchain_display_show_progress(uint8_t stage, uint8_t percent)
{ (void)stage; (void)percent; return 0; }
static inline int clearchain_display_show_result(clearchain_display_result_t result, uint8_t risk_score)
{ (void)result; (void)risk_score; return 0; }
#endif

#endif
