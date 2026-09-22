/**
 * @file envelope.c
 * @brief 设备端统一信封解析实现。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/crypto.h"
#include "common/envelope.h"
#include "common/error_code.h"

/** 拷贝字符串到定长缓冲，保证以 '\0' 结尾 */
static void copy_field(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, cap, "%s", src);
}

/** 取毫秒级时间戳（失败返回 0） */
static int64_t now_millis(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

/**
 * 生成 request_id：`<毫秒时间戳>-<8 位随机十六进制>`。
 *
 * 随机源不可用时退化为 `<毫秒时间戳>-<pid>`（仍满足 schema 的 8..128 长度要求，
 * 且不引入跨线程共享计数器的数据竞争）。
 */
static int build_request_id(char *buf, size_t cap, int64_t ts) {
    char suffix[16];

    if (wd_random_hex(suffix, sizeof(suffix)) > 0) {
        return snprintf(buf, cap, "%lld-%s", (long long)ts, suffix) > 0;
    }
    return snprintf(buf, cap, "%lld-%ld", (long long)ts, (long)getpid()) > 0;
}

char *envelope_wrap_request(const char *packet_type, const char *flat_json, char *request_id_out,
                            size_t request_id_cap) {
    cJSON *root = NULL;
    cJSON *header = NULL;
    cJSON *payload = NULL;
    cJSON *data = NULL;
    char request_id[WD_ENVELOPE_REQUEST_ID_CAP];
    char *out = NULL;
    int64_t ts;

    if (packet_type == NULL || packet_type[0] == '\0') {
        return NULL;
    }
    if (request_id_cap > sizeof(request_id)) {
        request_id_cap = sizeof(request_id);
    }

    ts = now_millis();
    if (!build_request_id(request_id, sizeof(request_id), ts)) {
        return NULL;
    }

    if (flat_json != NULL && flat_json[0] != '\0') {
        data = cJSON_Parse(flat_json);
        if (data == NULL || !cJSON_IsObject(data)) {
            /* 不是 JSON 或根节点不是对象：不猜测、不静默改写，交给调用方决策 */
            cJSON_Delete(data);
            return NULL;
        }
    } else {
        data = cJSON_CreateObject();
        if (data == NULL) {
            return NULL;
        }
    }

    root = cJSON_CreateObject();
    header = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (root == NULL || header == NULL || payload == NULL) {
        goto cleanup;
    }

    cJSON_AddStringToObject(header, "request_id", request_id);
    cJSON_AddStringToObject(header, "packet_type", packet_type);
    cJSON_AddNumberToObject(header, "timestamp", (double)ts);

    cJSON_AddStringToObject(payload, "code", WD_ENVELOPE_REQUEST_CODE);
    cJSON_AddStringToObject(payload, "message", "请求");
    cJSON_AddItemToObject(payload, "data", data);
    data = NULL; /* 所有权已转移给 payload */

    cJSON_AddItemToObject(root, "header", header);
    header = NULL;
    cJSON_AddItemToObject(root, "payload", payload);
    payload = NULL;

    out = cJSON_PrintUnformatted(root);

    if (out != NULL && request_id_out != NULL) {
        copy_field(request_id_out, request_id_cap, request_id);
    }

cleanup:
    cJSON_Delete(data);
    cJSON_Delete(header);
    cJSON_Delete(payload);
    cJSON_Delete(root);
    return out;
}

int envelope_parse(const char *json_str, wd_envelope_t *out)
{
    cJSON *root = NULL;
    cJSON *header = NULL;
    cJSON *payload = NULL;
    cJSON *code = NULL;

    if (out == NULL) {
        return WD_ENVELOPE_ERR_NULL;
    }

    /* 先清零：保证失败路径下 out 处于可安全 envelope_free 的状态 */
    memset(out, 0, sizeof(*out));

    if (json_str == NULL) {
        return WD_ENVELOPE_ERR_NULL;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        return WD_ENVELOPE_ERR_PARSE;
    }

    header = cJSON_GetObjectItemCaseSensitive(root, "header");
    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    if (!cJSON_IsObject(payload)) {
        cJSON_Delete(root);
        return WD_ENVELOPE_ERR_SHAPE;
    }

    code = cJSON_GetObjectItemCaseSensitive(payload, "code");
    if (!cJSON_IsString(code) || code->valuestring == NULL) {
        cJSON_Delete(root);
        return WD_ENVELOPE_ERR_CODE;
    }

    out->root = root;
    out->payload = payload;

    if (cJSON_IsObject(header)) {
        cJSON *request_id = cJSON_GetObjectItemCaseSensitive(header, "request_id");
        cJSON *packet_type = cJSON_GetObjectItemCaseSensitive(header, "packet_type");
        cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(header, "timestamp");

        if (cJSON_IsString(request_id)) {
            copy_field(out->header.request_id, sizeof(out->header.request_id), request_id->valuestring);
        }
        if (cJSON_IsString(packet_type)) {
            copy_field(out->header.packet_type, sizeof(out->header.packet_type), packet_type->valuestring);
        }
        if (cJSON_IsNumber(timestamp)) {
            out->header.timestamp = (int64_t)timestamp->valuedouble;
        }
    }

    return WD_ENVELOPE_OK;
}

void envelope_free(wd_envelope_t *env)
{
    if (env == NULL) {
        return;
    }
    if (env->root != NULL) {
        cJSON_Delete(env->root);
    }
    memset(env, 0, sizeof(*env));
}

const char *envelope_code(const wd_envelope_t *env)
{
    cJSON *code = NULL;

    if (env == NULL || env->payload == NULL) {
        return NULL;
    }
    code = cJSON_GetObjectItemCaseSensitive(env->payload, "code");
    return cJSON_IsString(code) ? code->valuestring : NULL;
}

const char *envelope_error_code(const wd_envelope_t *env)
{
    cJSON *error_code = NULL;

    if (env == NULL || env->payload == NULL) {
        return NULL;
    }
    error_code = cJSON_GetObjectItemCaseSensitive(env->payload, "errorCode");
    return cJSON_IsString(error_code) ? error_code->valuestring : NULL;
}

const char *envelope_message(const wd_envelope_t *env)
{
    cJSON *message = NULL;

    if (env == NULL || env->payload == NULL) {
        return NULL;
    }
    message = cJSON_GetObjectItemCaseSensitive(env->payload, "message");
    return cJSON_IsString(message) ? message->valuestring : NULL;
}

int envelope_is_success(const wd_envelope_t *env)
{
    const char *code = envelope_code(env);

    return wd_error_is_success(code);
}

cJSON *envelope_data(const wd_envelope_t *env)
{
    cJSON *data = NULL;

    if (env == NULL || env->payload == NULL) {
        return NULL;
    }
    data = cJSON_GetObjectItemCaseSensitive(env->payload, "data");
    if (data == NULL || cJSON_IsNull(data)) {
        return NULL;
    }
    return data;
}

cJSON *envelope_data_rows(const wd_envelope_t *env)
{
    cJSON *data = envelope_data(env);
    cJSON *rows = NULL;

    if (data == NULL) {
        return NULL;
    }
    if (cJSON_IsArray(data)) {
        return data;
    }
    if (cJSON_IsObject(data)) {
        rows = cJSON_GetObjectItemCaseSensitive(data, "rows");
        if (cJSON_IsArray(rows)) {
            return rows;
        }
    }
    return NULL;
}

const char *envelope_str(const cJSON *obj, const char *key)
{
    cJSON *item = NULL;

    if (obj == NULL || key == NULL) {
        return NULL;
    }
    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item)) {
        return item->valuestring;
    }
    return NULL;
}

int64_t envelope_int(const cJSON *obj, const char *key, int64_t default_value)
{
    cJSON *item = NULL;

    if (obj == NULL || key == NULL) {
        return default_value;
    }
    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        return (int64_t)item->valuedouble;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return strtol(item->valuestring, NULL, 10);
    }
    return default_value;
}
