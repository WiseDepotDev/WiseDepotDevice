/**
 * @file test_envelope.c
 * @brief 统一信封解析模块单元测试（common/envelope.c）。
 *
 * 覆盖 STD-CONTRACT-01 的设备端消费侧语义：
 * - 只接受 header + payload 的标准信封；
 * - 业务成功判定只看 payload.code（不再接受 "00000"/"0" 等历史兼容码）；
 * - 列表载荷支持 data 为数组或 data.rows；
 * - envelope_str/envelope_int 安全取字段。
 */

#include <assert.h>
#include <string.h>

#include "common/envelope.h"
#include "common/error_code.h"

/** 标准成功信封（data 为分页对象） */
static const char *SUCCESS_PAGED_JSON =
    "{"
    "\"header\":{\"request_id\":\"req-1\",\"packet_type\":\"INVENTORY_EXPECTED_LIST\",\"timestamp\":1772000000123},"
    "\"payload\":{\"code\":\"RES-0000\",\"message\":\"ok\",\"data\":{\"total\":1,\"rows\":["
    "{\"productId\":7,\"productName\":\"泵\",\"productCode\":\"P-7\",\"quantity\":2}]}}"
    "}";

/** 标准成功信封（data 直接是数组） */
static const char *SUCCESS_ARRAY_JSON =
    "{"
    "\"header\":{\"request_id\":\"req-2\",\"packet_type\":\"X\",\"timestamp\":1},"
    "\"payload\":{\"code\":\"RES-0000\",\"data\":[{\"productCode\":\"A-1\"}]}"
    "}";

/** 业务失败信封 */
static const char *BUSINESS_ERROR_JSON =
    "{"
    "\"header\":{\"request_id\":\"req-3\",\"packet_type\":\"AUTH_LOGIN\",\"timestamp\":2},"
    "\"payload\":{\"code\":\"AUTH-REQUEST-1002\",\"message\":\"用户名或密码错误\","
    "\"errorCode\":\"AUTH-REQUEST-1002\",\"data\":null}"
    "}";

/** 缺少 payload 的非法信封 */
static const char *NO_PAYLOAD_JSON =
    "{\"header\":{\"request_id\":\"req-4\"}}";

/** 历史兼容格式（根节点直接带 code）：必须被拒绝 */
static const char *LEGACY_JSON =
    "{\"code\":\"00000\",\"data\":[{\"productCode\":\"L-1\"}]}";

/** 解析成功信封应取到 header 与列表（data.rows 路径） */
void test_envelope_success_paged(void)
{
    wd_envelope_t env;
    cJSON *rows;
    cJSON *first;

    assert(envelope_parse(SUCCESS_PAGED_JSON, &env) == WD_ENVELOPE_OK);
    assert(envelope_is_success(&env) == 1);
    assert(strcmp(envelope_code(&env), "RES-0000") == 0);
    assert(strcmp(env.header.request_id, "req-1") == 0);
    assert(strcmp(env.header.packet_type, "INVENTORY_EXPECTED_LIST") == 0);
    assert(env.header.timestamp == 1772000000123LL);

    rows = envelope_data_rows(&env);
    assert(cJSON_IsArray(rows));
    first = cJSON_GetArrayItem(rows, 0);
    assert(first != NULL);
    assert(strcmp(envelope_str(first, "productCode"), "P-7") == 0);
    assert(strcmp(envelope_str(first, "productName"), "泵") == 0);
    assert(envelope_int(first, "quantity", 0) == 2);
    assert(envelope_int(first, "missing", 42) == 42);
    assert(envelope_str(first, "missing") == NULL);

    envelope_free(&env);
}

/** data 直接是数组时也应作为列表返回 */
void test_envelope_success_array_data(void)
{
    wd_envelope_t env;

    assert(envelope_parse(SUCCESS_ARRAY_JSON, &env) == WD_ENVELOPE_OK);
    assert(envelope_is_success(&env) == 1);
    assert(cJSON_IsArray(envelope_data_rows(&env)));
    assert(cJSON_IsArray(envelope_data(&env)));
    envelope_free(&env);
}

/** 业务失败信封：解析成功但不应判定为成功，且能取到 errorCode */
void test_envelope_business_failure(void)
{
    wd_envelope_t env;

    assert(envelope_parse(BUSINESS_ERROR_JSON, &env) == WD_ENVELOPE_OK);
    assert(envelope_is_success(&env) == 0);
    assert(strcmp(envelope_code(&env), "AUTH-REQUEST-1002") == 0);
    assert(strcmp(envelope_error_code(&env), "AUTH-REQUEST-1002") == 0);
    assert(strcmp(envelope_message(&env), "用户名或密码错误") == 0);
    assert(envelope_data_rows(&env) == NULL);
    envelope_free(&env);
}

/** 缺少 payload 应返回结构错误，且不得泄漏解析树 */
void test_envelope_missing_payload(void)
{
    wd_envelope_t env;

    assert(envelope_parse(NO_PAYLOAD_JSON, &env) == WD_ENVELOPE_ERR_SHAPE);
    assert(env.root == NULL);
}

/** 历史兼容格式（根级 code + "00000"）必须被拒绝 */
void test_envelope_rejects_legacy_format(void)
{
    wd_envelope_t env;

    assert(envelope_parse(LEGACY_JSON, &env) == WD_ENVELOPE_ERR_SHAPE);
    assert(env.root == NULL);
}

/** 非法输入与空指针应安全返回 */
void test_envelope_invalid_input(void)
{
    wd_envelope_t env;

    assert(envelope_parse(NULL, &env) == WD_ENVELOPE_ERR_NULL);
    assert(envelope_parse("{not json", &env) == WD_ENVELOPE_ERR_PARSE);
    assert(envelope_parse(SUCCESS_ARRAY_JSON, NULL) == WD_ENVELOPE_ERR_NULL);

    /* envelope_free 必须幂等且可接受 NULL */
    envelope_free(NULL);
    envelope_free(&env);
}

/** 成功码宏应与信封判定一致 */
void test_envelope_success_code_matches_generated_macro(void)
{
    wd_envelope_t env;

    assert(envelope_parse(SUCCESS_PAGED_JSON, &env) == WD_ENVELOPE_OK);
    assert(strcmp(envelope_code(&env), WD_ERROR_SUCCESS) == 0);
    assert(wd_error_is_success(envelope_code(&env)) == 1);
    envelope_free(&env);
}

/**
 * 系统失败样例（对应 docs/standards/fixtures/envelope-system-error.json）。
 *
 * 三端契约测试共用同一组样例：packet_type 为 UNKNOWN、code 与 errorCode 均为 SYS-REQUEST-1001。
 */
void test_envelope_system_error_fixture(void)
{
    static const char *SYSTEM_ERROR_JSON =
        "{"
        "\"header\":{\"request_id\":\"2026-02-27T12:00:02.789+08:00#ghi789\","
        "\"packet_type\":\"UNKNOWN\",\"timestamp\":1772000002789},"
        "\"payload\":{\"code\":\"SYS-REQUEST-1001\",\"message\":\"未知异常\","
        "\"errorCode\":\"SYS-REQUEST-1001\",\"data\":null}"
        "}";
    wd_envelope_t env;

    assert(envelope_parse(SYSTEM_ERROR_JSON, &env) == WD_ENVELOPE_OK);
    assert(envelope_is_success(&env) == 0);
    assert(strcmp(env.header.packet_type, "UNKNOWN") == 0);
    assert(env.header.timestamp == 1772000002789LL);
    assert(strcmp(envelope_code(&env), WD_SYSTEM_REQUEST_ERROR) == 0);
    assert(strcmp(envelope_error_code(&env), WD_SYSTEM_REQUEST_ERROR) == 0);
    assert(envelope_data(&env) == NULL);
    assert(envelope_data_rows(&env) == NULL);
    envelope_free(&env);
}
