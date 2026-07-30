#ifndef CLEARCHAIN_FEEDBACK_H
#define CLEARCHAIN_FEEDBACK_H

void clearchain_feedback_init(void);
void clearchain_feedback_standby(void);
void clearchain_feedback_tag_read(void);
void clearchain_feedback_post_success(void);
void clearchain_feedback_post_failed(void);
void clearchain_feedback_risk_alert(void);

#endif
