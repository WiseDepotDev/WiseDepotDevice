/**
 * @file test_wd_error.c
 * @brief 统一错误码的契约用例（STD-CODE-06 / P4-11）。
 *
 * 这里锁死三件事，防止后续有人图省事把具名错误码改回裸魔数：
 * 1. `WD_OK` 必须是 0，其余必须全为负（迁移期 `!= 0` / `< 0` 调用点才成立）；
 * 2. 各错误码互不相同（否则日志会把两种故障混成一种）；
 * 3. `wd_error_str()` 名字稳定（日志与排障脚本按名字检索）。
 */

#include "common/wd_error.h"
#include "unity.h"

void test_wd_error_values_are_non_positive(void) {
    TEST_ASSERT_EQUAL(0, WD_OK);

    const wd_error_t errors[] = {
        WD_ERR_PARAM, WD_ERR_TIMEOUT, WD_ERR_PROTOCOL, WD_ERR_CRC, WD_ERR_IO,
        WD_ERR_NOMEM, WD_ERR_STATE, WD_ERR_NOT_FOUND, WD_ERR_BUSY, WD_ERR_FULL,
        WD_ERR_UNSUPPORTED, WD_ERR_CRYPTO, WD_ERR_CONNECT, WD_ERR_CANCELED,
        WD_ERR_ACTION, WD_ERR_GENERAL
    };
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        TEST_ASSERT_TRUE(errors[i] < 0);
    }
}

void test_wd_error_codes_are_distinct(void) {
    const wd_error_t errors[] = {
        WD_OK, WD_ERR_PARAM, WD_ERR_TIMEOUT, WD_ERR_PROTOCOL, WD_ERR_CRC, WD_ERR_IO,
        WD_ERR_NOMEM, WD_ERR_STATE, WD_ERR_NOT_FOUND, WD_ERR_BUSY, WD_ERR_FULL,
        WD_ERR_UNSUPPORTED, WD_ERR_CRYPTO, WD_ERR_CONNECT, WD_ERR_CANCELED,
        WD_ERR_ACTION, WD_ERR_GENERAL
    };
    const unsigned n = sizeof(errors) / sizeof(errors[0]);
    for (unsigned i = 0; i < n; i++) {
        for (unsigned j = i + 1; j < n; j++) {
            TEST_ASSERT_TRUE(errors[i] != errors[j]);
        }
    }
}

void test_wd_error_str_is_stable(void) {
    TEST_ASSERT_EQUAL_STRING("WD_OK", wd_error_str(WD_OK));
    TEST_ASSERT_EQUAL_STRING("WD_ERR_PARAM", wd_error_str(WD_ERR_PARAM));
    TEST_ASSERT_EQUAL_STRING("WD_ERR_TIMEOUT", wd_error_str(WD_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("WD_ERR_CRC", wd_error_str(WD_ERR_CRC));
    TEST_ASSERT_EQUAL_STRING("WD_ERR_NOMEM", wd_error_str(WD_ERR_NOMEM));
    TEST_ASSERT_EQUAL_STRING("WD_ERR_GENERAL", wd_error_str(WD_ERR_GENERAL));
    TEST_ASSERT_EQUAL_STRING("WD_ERR_UNKNOWN", wd_error_str((wd_error_t)-999));
}
