#ifndef TEST_SOC_OSAL_H
#define TEST_SOC_OSAL_H
#include "osal_debug.h"
#include <stdint.h>
typedef void *(*osal_kthread_handler)(void *);
typedef struct { int unused; } osal_task;
osal_task *osal_kthread_create(osal_kthread_handler fn,void *arg,const char *name,uint32_t stack);
void osal_kfree(void *p);
int osal_msleep(unsigned int ms);
#endif
