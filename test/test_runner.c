#include "unity.h"
#include <stdio.h>

/* Test Prototypes */
extern void test_xmalloc(void);
extern void test_xmalloc_try_returns_null_on_failure(void);
extern void test_business_path_degrades_on_oom(void);
extern void test_config(void);
extern void test_config_signature_secret(void);
extern void test_config_env_overrides_file(void);
extern void test_sys_monitor(void);
extern void test_device_info(void);
extern void test_scheduler(void);
extern void test_backoff_strategy(void);
extern void test_inventory_load_expected(void);
extern void test_inventory_process_scan(void);
extern void test_envelope_success_paged(void);
extern void test_envelope_success_array_data(void);
extern void test_envelope_business_failure(void);
extern void test_envelope_missing_payload(void);
extern void test_envelope_rejects_legacy_format(void);
extern void test_envelope_invalid_input(void);
extern void test_envelope_success_code_matches_generated_macro(void);
extern void test_envelope_system_error_fixture(void);
/* P4-01 内存安全回归 */
extern void test_bytes_to_hex_respects_capacity(void);
extern void test_wd_str_appendf_never_overflows(void);
extern void test_patrol_task_to_json_clamps_action_count(void);
/* P4-03 日志并发安全 */
extern void test_logger_multithreaded_output_is_intact(void);
extern void test_logger_reopen_keeps_logging(void);

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
    RUN_TEST(test_xmalloc_try_returns_null_on_failure);
    RUN_TEST(test_business_path_degrades_on_oom);
    RUN_TEST(test_config);
    RUN_TEST(test_config_signature_secret);
    RUN_TEST(test_config_env_overrides_file);
    RUN_TEST(test_sys_monitor);
    RUN_TEST(test_device_info);
    RUN_TEST(test_scheduler);
    RUN_TEST(test_backoff_strategy);
    
    // RFID Tests
    RUN_TEST(test_inventory_load_expected);
    RUN_TEST(test_inventory_process_scan);

    // 统一信封解析（STD-CONTRACT-01 设备端消费侧）
    RUN_TEST(test_envelope_success_paged);
    RUN_TEST(test_envelope_success_array_data);
    RUN_TEST(test_envelope_business_failure);
    RUN_TEST(test_envelope_missing_payload);
    RUN_TEST(test_envelope_rejects_legacy_format);
    RUN_TEST(test_envelope_invalid_input);
    RUN_TEST(test_envelope_success_code_matches_generated_macro);
    RUN_TEST(test_envelope_system_error_fixture);

    // 内存安全（P4-01）
    RUN_TEST(test_bytes_to_hex_respects_capacity);
    RUN_TEST(test_wd_str_appendf_never_overflows);
    RUN_TEST(test_patrol_task_to_json_clamps_action_count);

    // 日志并发安全（P4-03）
    RUN_TEST(test_logger_multithreaded_output_is_intact);
    RUN_TEST(test_logger_reopen_keeps_logging);

    return UNITY_END();
}
