#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
/* P4-07 任务生命周期与取消语义 */
extern void test_patrol_cancel_is_per_task(void);
extern void test_patrol_execute_cancelled_midway(void);
extern void test_patrol_execute_cancelled_on_last_action(void);
extern void test_patrol_execute_completes_normally(void);
extern void test_patrol_new_task_is_not_cancelled(void);
extern void test_rfid_build_frame_matches_manual_layout(void);
extern void test_rfid_build_frame_rejects_bad_input(void);
extern void test_rfid_config_env_overrides(void);
extern void test_rfid_baud_code_matches_reference(void);
extern void test_rfid_set_config_rejects_bad_params(void);
extern void test_patrol_task_from_server_message(void);
extern void test_patrol_task_from_actions_array(void);
extern void test_patrol_task_from_json_rejects_bad_input(void);
extern void test_patrol_task_id_is_numeric(void);
extern void test_patrol_task_json_roundtrip(void);
extern void test_patrol_task_json_from_response(void);
extern void test_config_persistent_roundtrip(void);
extern void test_wd_error_values_are_non_positive(void);
extern void test_wd_error_codes_are_distinct(void);
extern void test_wd_error_str_is_stable(void);

/* Setup and Teardown for Unity */
void setUp(void) {
    // Default setup
}

void tearDown(void) {
    // Default teardown
}

int main(void) {
    /* P4-17：把测试的工作目录切到临时目录。
     * 起因：config 持久化读回上线后，`test_config` 断言默认值时被仓库根目录里
     * 遗留的 wise-device.dat 覆盖而失败——用例不该依赖"当前目录恰好干净"。
     * 日志、盘点缓存、持久化配置都会写 CWD，统一在这里隔离。 */
    char tmpl[] = "/tmp/wise-depot-tests-XXXXXX";
    if (mkdtemp(tmpl) != NULL) {
        if (chdir(tmpl) != 0) {
            fprintf(stderr, "[WARN] 无法切到临时测试目录 %s\n", tmpl);
        } else {
            printf("[TEST] 测试工作目录: %s\n", tmpl);
        }
    } else {
        fprintf(stderr, "[WARN] mkdtemp 失败，测试将在当前目录运行\n");
    }

    UNITY_BEGIN();
    
    RUN_TEST(test_xmalloc);
    RUN_TEST(test_xmalloc_try_returns_null_on_failure);
    RUN_TEST(test_business_path_degrades_on_oom);
    RUN_TEST(test_config);
    RUN_TEST(test_config_signature_secret);
    RUN_TEST(test_config_env_overrides_file);
    RUN_TEST(test_config_persistent_roundtrip);   /* P4-17 */
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

    // 任务生命周期与取消语义（P4-07）
    RUN_TEST(test_patrol_cancel_is_per_task);
    RUN_TEST(test_patrol_execute_cancelled_midway);
    RUN_TEST(test_patrol_execute_cancelled_on_last_action);
    RUN_TEST(test_patrol_execute_completes_normally);
    RUN_TEST(test_patrol_new_task_is_not_cancelled);

    // RFID 组帧与读头参数（P4-12）
    RUN_TEST(test_rfid_build_frame_matches_manual_layout);
    RUN_TEST(test_rfid_build_frame_rejects_bad_input);
    RUN_TEST(test_rfid_config_env_overrides);
    RUN_TEST(test_rfid_baud_code_matches_reference);        /* P4-18 */
    RUN_TEST(test_rfid_set_config_rejects_bad_params);       /* P4-18 */

    // 巡检任务 JSON 与服务端契约对齐（P4-16）
    RUN_TEST(test_patrol_task_from_server_message);
    RUN_TEST(test_patrol_task_from_actions_array);
    RUN_TEST(test_patrol_task_from_json_rejects_bad_input);
    RUN_TEST(test_patrol_task_id_is_numeric);
    RUN_TEST(test_patrol_task_json_roundtrip);
    RUN_TEST(test_patrol_task_json_from_response);

    // 统一错误码契约（P4-11 / STD-CODE-06）
    RUN_TEST(test_wd_error_values_are_non_positive);
    RUN_TEST(test_wd_error_codes_are_distinct);
    RUN_TEST(test_wd_error_str_is_stable);

    return UNITY_END();
}
