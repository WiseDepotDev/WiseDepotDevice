/**
 * 配置服务模块实现
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-13
 */

#include "application/config_service.h"
#include "application/device_service.h"
#include "common/config.h"
#include "common/logger.h"
#include "infrastructure/http_client.h"
#include "common/crypto.h"
#include "common/xmalloc.h"
#include "common/wd_error.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

wd_error_t config_service_init(void) {
    LOG_INFO("Config service initialized");
    return 0;
}

/**
 * 启动阶段的一次性远端配置拉取（P4-08：由 common/config.c 迁入）
 *
 * 与 config_fetch_task() 的分工：本函数在设备尚未注册、没有 Token 时做"最佳努力"拉取，
 * 因此不带签名/鉴权头；带签名与 Token 的定时拉取仍由 config_fetch_task() 负责。
 * 失败只告警，不影响启动（配置保持本地值）。
 */
int config_service_fetch_remote(void) {
    const wd_config_t *cfg = config_get();
    if (!cfg || !cfg->server_url || !cfg->device_id) {
        LOG_WARN("Skip remote config: server_url or device_id missing");
        return WD_ERR_STATE;
    }

    /* URL 形如：SERVER_URL + api_base_url + /config?deviceId=...&version=... */
    char url[1024];
    snprintf(url, sizeof(url), "%s%s/config?deviceId=%s&version=%s",
             cfg->server_url, cfg->api_base_url, cfg->device_id, cfg->version);

    LOG_INFO("Fetching config from: %s", url);
    http_response_t *res = http_get(url, NULL, 0);
    if (!res) {
        LOG_WARN("Failed to connect to config server");
        return WD_ERR_CONNECT;
    }

    int rc = -1;
    if (res->status_code == 200 && res->body) {
        LOG_INFO("Config fetched successfully");
        rc = config_update_from_json(res->body);
    } else {
        LOG_WARN("Failed to fetch config, status: %d", res->status_code);
    }
    http_response_free(res);
    return rc;
}

void config_fetch_task(void *ctx) {
    (void)ctx;
    const wd_config_t *cfg = config_get();
    
    // Add signature headers
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL)); 
    
    char nonce[20];
    snprintf(nonce, sizeof(nonce), "%d", rand());
    
    char query_string[1024];
    snprintf(query_string, sizeof(query_string), "deviceId=%s&version=%s", cfg->device_id, cfg->version);

    char url[2048];
    snprintf(url, sizeof(url), "%s%s/config?%s", 
             cfg->server_url, cfg->api_base_url, query_string);

    char string_to_sign[4096];
    char uri_path[256];
    // 使用与服务端 DeviceController 映射一致的路径: /api/device/config
    snprintf(uri_path, sizeof(uri_path), "%s/config", cfg->api_base_url);
    
    /* 拉取路径 = api_base_url + /config，与服务端 DeviceController 的类级映射一致
     * （config_init_defaults 里的默认 api_base_url 为 /api/device）。 */
    snprintf(uri_path, sizeof(uri_path), "%s/config", cfg->api_base_url);

    /* 签名串：METHOD \n URI \n SortedQueryString（服务端校验逻辑，改动需同步服务端与 APP）。 */
    
    // 获取 Token（声明见 application/device_service.h）
    char *token = device_service_get_token();
    
    // 构造签名 Query
    char sign_query_string[2048];
    snprintf(sign_query_string, sizeof(sign_query_string), 
             "deviceId=%s&nonce=%s&timestamp=%s&version=%s", 
             cfg->device_id, nonce, timestamp, cfg->version);

    snprintf(string_to_sign, sizeof(string_to_sign), "GET\n%s\n%s", 
             uri_path, sign_query_string);
    
    unsigned char hmac_result[32];
    const char *signing_secret = config_signature_secret();
    if (signing_secret == NULL) {
        LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); config request aborted");
        return;
    }
    hmac_sha256(signing_secret, strlen(signing_secret), string_to_sign, strlen(string_to_sign), hmac_result);
    
    size_t sig_len = 0;
    char *signature = base64_encode(hmac_result, 32, &sig_len);
    
    char header_sign[256];
    char header_time[64];
    char header_nonce[64];
    
    snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
    snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
    snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
    
    // 构建 Headers
    int header_count = 3;
    const char *headers[5];
    headers[0] = header_sign;
    headers[1] = header_time;
    headers[2] = header_nonce;
    
    char header_auth[1024];
    if (token) {
        snprintf(header_auth, sizeof(header_auth), "Authorization: Bearer %s", token);
        headers[3] = header_auth;
        header_count++;
    }
    
    LOG_DEBUG("Fetching config from %s", url);
    
    http_response_t *res = http_get(url, headers, header_count);
    
    xfree(signature);
    if (!res) {
        LOG_WARN("Failed to fetch config (network error)");
        return;
    }
    
    if (res->status_code == 304) {
        LOG_DEBUG("Config not modified");
        http_response_free(res);
        return;
    }
    
    if (res->status_code == 200 && res->body) {
        /* 响应体未做签名校验：当前依赖 HTTPS 通道（若要校验响应签名，服务端需回 X-Signature）。 */
        
        LOG_INFO("Received new config version");
        if (config_update_from_json(res->body) == 0) {
            LOG_INFO("Config updated successfully");
        } else {
            LOG_ERROR("Failed to update config from response");
        }
    } else {
        LOG_WARN("Config fetch failed: %d", res->status_code);
    }
    
    http_response_free(res);
}

void config_service_stop(void) {
    // No resources to clean up specifically
}
