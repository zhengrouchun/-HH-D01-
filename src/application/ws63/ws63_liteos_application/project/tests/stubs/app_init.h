#ifndef TEST_APP_INIT_H
#define TEST_APP_INIT_H
#define app_run(fn) static void (*test_app_entry)(void)=fn
#endif
