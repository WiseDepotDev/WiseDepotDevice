/**
 * @file envelope.c
 * @brief 设备端统一信封解析实现。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
