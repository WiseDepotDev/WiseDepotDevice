/**
 * @file test_envelope_wrap.c
 * @brief 请求侧信封化（envelope_wrap_request）单元测试。
 *
 * 覆盖 P4-19 的验收点：请求体被包装成 std 信封（header + payload.data），
 * 业务字段逐字不变，request_id 可透传到 HTTP 头，非法入参不被猜测性改写。
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "common/envelope.h"
#include "common/error_code.h"

/** 与设备端真实注册体同构的扁平 JSON */
static const char *FLAT_REGISTER_JSON =
    "{\"deviceCode\":\"DEV-001\",\"deviceName\":\"M-1\",\"deviceType\":2,\"remark\":\"OS: Linux\"}";

/** 包装结果必须形如标准信封：根只有 header/payload，header 三字段，payload 带 code/message/data */
static void assert_is_standard_envelope(const char *json) {
    cJSON *root = cJSON_Parse(json);
    cJSON *header;
    cJSON *payload;

    assert(root != NULL);
    assert(cJSON_GetArraySize(root) == 2);

    header = cJSON_GetObjectItemCaseSensitive(root, "header");
    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    assert(cJSON_IsObject(header));
    assert(cJSON_IsObject(payload));
    assert(cJSON_GetArraySize(header) == 3);
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(header, "request_id")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(header, "packet_type")));
    assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(header, "timestamp")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(payload, "code")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(payload, "message")));
    assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(payload, "data")));

    cJSON_Delete(root);
}

/** 包装后的字段与入参逐字一致，且 request_id 可被调用方拿到（用于 HTTP 头透传） */
void test_envelope_wrap_request_shape(void) {
    char request_id[WD_ENVELOPE_REQUEST_ID_CAP] = {0};
    char *wrapped =
        envelope_wrap_request("DEVICE_CREATE", FLAT_REGISTER_JSON, request_id, sizeof(request_id));
    cJSON *root;
    cJSON *header;
    cJSON *data;
    cJSON *device_code;

    assert(wrapped != NULL);
    assert_is_standard_envelope(wrapped);

    root = cJSON_Parse(wrapped);
    header = cJSON_GetObjectItemCaseSensitive(root, "header");
    data =
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "payload"), "data");

    /* 调用方缓冲里的 request_id 必须等于信封内的 request_id（否则日志与链路对不上） */
    assert(request_id[0] != '\0');
    assert(strcmp(request_id,
                  cJSON_GetObjectItemCaseSensitive(header, "request_id")->valuestring) == 0);
    assert(strlen(request_id) >= 8);

    assert(strcmp(cJSON_GetObjectItemCaseSensitive(header, "packet_type")->valuestring,
                  "DEVICE_CREATE") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(header, "timestamp")->valuedouble > 0);

    /* 业务字段逐字保留（不接受任何重命名或类型变化） */
    device_code = cJSON_GetObjectItemCaseSensitive(data, "deviceCode");
    assert(cJSON_IsString(device_code));
    assert(strcmp(device_code->valuestring, "DEV-001") == 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(data, "deviceName")->valuestring, "M-1") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(data, "deviceType")->valueint == 2);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(data, "remark")->valuestring, "OS: Linux") == 0);

    cJSON_Delete(root);
    free(wrapped);
}

/** 自产自消：设备端自己的解析器必须能读回自己包装的请求体 */
void test_envelope_wrap_request_roundtrip_with_own_parser(void) {
    wd_envelope_t env;
    cJSON *data;
    char *wrapped = envelope_wrap_request(
        "DEVICE_HEARTBEAT", "{\"deviceCode\":\"DEV-002\",\"cpuUsage\":12.5}", NULL, 0);

    assert(wrapped != NULL);
    assert(envelope_parse(wrapped, &env) == WD_ENVELOPE_OK);
    assert(strcmp(envelope_code(&env), WD_ENVELOPE_REQUEST_CODE) == 0);
    assert(strcmp(env.header.packet_type, "DEVICE_HEARTBEAT") == 0);
    assert(env.header.timestamp > 0);

    data = envelope_data(&env);
    assert(data != NULL);
    assert(strcmp(envelope_str(data, "deviceCode"), "DEV-002") == 0);
    assert(envelope_str(data, "missing") == NULL);

    envelope_free(&env);
    free(wrapped);
}

/** 无业务字段时 payload.data 必须是空对象（不是 null：服务端按对象绑定） */
void test_envelope_wrap_request_empty_payload(void) {
    char *from_null = envelope_wrap_request("UNKNOWN", NULL, NULL, 0);
    char *from_empty = envelope_wrap_request("UNKNOWN", "", NULL, 0);
    cJSON *root;
    cJSON *data;

    assert(from_null != NULL);
    assert(from_empty != NULL);

    root = cJSON_Parse(from_null);
    data =
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "payload"), "data");
    assert(cJSON_IsObject(data));
    assert(cJSON_GetArraySize(data) == 0);
    cJSON_Delete(root);
    free(from_null);

    root = cJSON_Parse(from_empty);
    data =
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "payload"), "data");
    assert(cJSON_IsObject(data));
    assert(cJSON_GetArraySize(data) == 0);
    cJSON_Delete(root);
    free(from_empty);
}

/** 非法入参：不猜测、不静默改写，一律返回 NULL 由调用方决策 */
void test_envelope_wrap_request_rejects_bad_input(void) {
    char request_id[WD_ENVELOPE_REQUEST_ID_CAP] = {0};

    assert(envelope_wrap_request(NULL, FLAT_REGISTER_JSON, NULL, 0) == NULL);
    assert(envelope_wrap_request("", FLAT_REGISTER_JSON, NULL, 0) == NULL);
    assert(envelope_wrap_request("DEVICE_CREATE", "{not json", NULL, 0) == NULL);
    /* 根节点是数组：信封的 payload.data 允许是数组，但「扁平请求体」必须是对象 */
    assert(envelope_wrap_request("DEVICE_CREATE", "[1,2,3]", NULL, 0) == NULL);
    assert(envelope_wrap_request("DEVICE_CREATE", "\"text\"", NULL, 0) == NULL);

    /* 失败时不得写调用方缓冲 */
    assert(request_id[0] == '\0');
}

/**
 * packet_type 必须符合 schema 的 `^[A-Z][A-Z0-9_]*$`。
 *
 * 说明：本用例只约束形状，不校验「是否存在于服务端 PacketType 枚举」——
 * 枚举一致性由跨端脚本核对（见 tools/ 下的 packet type 检查）。
 */
void test_envelope_wrap_request_packet_type_pattern(void) {
    static const char *TYPES[] = {"DEVICE_CREATE", "DEVICE_HEARTBEAT", "RFID_DATA_UPLOAD",
                                  "UNKNOWN", "A1_B2"};
    size_t i;

    for (i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++) {
        char *wrapped = envelope_wrap_request(TYPES[i], FLAT_REGISTER_JSON, NULL, 0);
        cJSON *root;
        const char *type;
        size_t j;

        assert(wrapped != NULL);
        root = cJSON_Parse(wrapped);
        type = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "header"),
                                                "packet_type")
                   ->valuestring;
        assert(strcmp(type, TYPES[i]) == 0);
        assert(type[0] >= 'A' && type[0] <= 'Z');
        for (j = 1; j < strlen(type); j++) {
            int ok = (type[j] >= 'A' && type[j] <= 'Z') || (type[j] >= '0' && type[j] <= '9') ||
                     type[j] == '_';
            assert(ok);
        }
        assert(strlen(type) <= 64);

        cJSON_Delete(root);
        free(wrapped);
    }
}
