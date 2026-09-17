#include "unity.h"
#include <stdio.h>

/* Test Prototypes */
extern void test_xmalloc(void);
extern void test_config(void);
extern void test_sys_monitor(void);
extern void test_device_info(void);
extern void test_scheduler(void);
extern void test_backoff_strategy(void);
extern void test_inventory_load_expected(void);
extern void test_inventory_process_scan(void);

/* Setup and Teardown for Unity */
void setUp(void) {
    // Default setup
}

void tearDown(void) {
    // Default teardown
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_xmalloc);
    RUN_TEST(test_config);
    RUN_TEST(test_sys_monitor);
    RUN_TEST(test_device_info);
    RUN_TEST(test_scheduler);
    RUN_TEST(test_backoff_strategy);
    
    // RFID Tests
    RUN_TEST(test_inventory_load_expected);
    RUN_TEST(test_inventory_process_scan);
    
    return UNITY_END();
}
