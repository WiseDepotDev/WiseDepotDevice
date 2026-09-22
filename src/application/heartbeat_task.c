/**
 * 心跳任务模块实现
 *
 * @author xingchentye
 * @version 0.3.0
 * @since 2026-03-06
 */

#include "application/heartbeat_task.h"
#include "application/device_service.h"
#include "common/config.h"
#include "common/envelope.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "common/sys_monitor.h"
#include "infrastructure/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/crypto.h"
#include <time.h>

/* 全局 Token */
static char *g_hb_token = NULL;
/* 签名密钥：统一走 config_signature_secret()（环境变量 / 配置文件注入，源码内无默认值，P4-04） */

void heartbeat_set_token(const char *token) {
    if (token) {
        /* P4-06：先复制成功再替换，失败时保持原令牌不变 */
        char *copy = xstrdup_try(token);
        if (!copy) {
            LOG_ERROR("保存心跳令牌失败（内存不足），保持原令牌不变");
            return;
        }
        if (g_hb_token) xfree(g_hb_token);
        g_hb_token = copy;
        return;
    }
    if (g_hb_token) {
        xfree(g_hb_token);
        g_hb_token = NULL;
    }
}

void heartbeat_clear_token(void) {
    if (g_hb_token) {
        xfree(g_hb_token);
        g_hb_token = NULL;
    }
}

void heartbeat_task_execute(void *ctx) {
    (void)ctx;
    
    // 如果没有 Token，尝试使用签名 (如果服务器允许)
    // 但心跳任务本身不应直接调用 register，而是报告错误让主控决定
    
    const wd_config_t *cfg = config_get();
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/device/heartbeat", cfg->server_url);
    
    double cpu = sys_monitor_get_cpu_usage();
    double mem = sys_monitor_get_mem_usage();
    
    char body[1024];
    // Server expects "deviceCode" instead of "deviceId" based on error logs
    snprintf(body, sizeof(body), 
             "{\"deviceCode\": \"%s\", \"cpuUsage\": %.2f, \"memUsage\": %.2f}",
             cfg->device_id, cpu, mem);

    /* P4-19 请求侧信封化：后端 DeviceController#receiveHeartbeat 的 @ApiPacketType 即 DEVICE_HEARTBEAT。
     * 只包装「带 JSON 体」的两种调用；无 Token 走的 query 形式本来就没有请求体，保持原样。
     * 决策 3：信封化失败即中止本次心跳（服务端不再接受扁平体），下个周期自然重试。 */
    char request_id[WD_ENVELOPE_REQUEST_ID_CAP] = {0};
    char *enveloped = envelope_wrap_request("DEVICE_HEARTBEAT", body, request_id, sizeof(request_id));
    if (enveloped == NULL) {
        LOG_ERROR("心跳请求信封化失败，本次心跳中止（决策 3：三端只支持统一信封）");
        return;
    }
    const char *body_to_send = enveloped;
    
    http_response_t *res = NULL;
    
    if (g_hb_token) {
        // Use Token
        char auth_header[1024];
        char header_request_id[192];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_hb_token);
        const char *headers[2];
        int header_count = 1;
        headers[0] = auth_header;
        if (request_id[0] != '\0') {
            snprintf(header_request_id, sizeof(header_request_id), "REQUEST-ID: %s", request_id);
            headers[header_count++] = header_request_id;
        }
        res = http_post(url, body_to_send, headers, header_count);
    } else {
        // Fallback to Signature
        LOG_DEBUG("No token, using signature for heartbeat");
        
        char timestamp[20];
        snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
        
        char nonce[32];
        if (wd_random_hex(nonce, sizeof(nonce)) == 0) {
        LOG_ERROR("无法获取随机 nonce，心跳中止");
        free(enveloped);
        return;
    }
        
        // deviceCode is in body, not query param, so don't include in signature
        char query_string[1024];
        snprintf(query_string, sizeof(query_string), "nonce=%s&timestamp=%s", nonce, timestamp);
        
        char string_to_sign[2048];
        const char *uri_path = "/api/device/heartbeat";
        snprintf(string_to_sign, sizeof(string_to_sign), "POST\n%s\n%s", uri_path, query_string);
        
        unsigned char hmac_result[32];
        const char *signing_secret = config_signature_secret();
        if (signing_secret == NULL) {
            LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); heartbeat skipped");
            free(enveloped);
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
        
        const char *headers[] = { header_sign, header_time, header_nonce };
        
        // Use query param for deviceCode
        char url_with_query[2048];
        snprintf(url_with_query, sizeof(url_with_query), "%s?deviceCode=%s", url, cfg->device_id);
        
        res = http_post(url_with_query, NULL, headers, 3);
        
        xfree(signature);
    }
    
    if (!res) {
        LOG_ERROR("Heartbeat request failed (Network error)");
        free(enveloped);
        return;
    }
    
    if (res->status_code == 401 || res->status_code == 403) {
        LOG_WARN("Heartbeat unauthorized (Status: %d), attempting token refresh", res->status_code);
        
        // Try to refresh token first
        if (device_refresh_token() == 0) {
            // Refresh successful, retry heartbeat
            http_response_free(res);
            LOG_INFO("Token refreshed, retrying heartbeat");
            
            // Retry with new token：复用同一个信封与 request_id（同一次逻辑请求的重试）
            char auth_header[1024];
            char retry_request_id[192];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_hb_token);
            const char *headers[2];
            int header_count = 1;
            headers[0] = auth_header;
            if (request_id[0] != '\0') {
                snprintf(retry_request_id, sizeof(retry_request_id), "REQUEST-ID: %s", request_id);
                headers[header_count++] = retry_request_id;
            }
            res = http_post(url, body_to_send, headers, header_count);
            
            if (res && res->status_code >= 200 && res->status_code < 300) {
                // LOG_DEBUG("Heartbeat successful after token refresh");
                http_response_free(res);
                free(enveloped);
                return;
            }
        }
        
        // Refresh failed, clear token and signal re-registration
        heartbeat_clear_token();
        device_trigger_reauth();
    } else if (res->status_code >= 200 && res->status_code < 300) {
        // LOG_DEBUG("Heartbeat successful");
    } else {
        LOG_ERROR("Heartbeat failed (Status: %d)", res->status_code);
    }
    
    if (res) {
        http_response_free(res);
    }
    free(enveloped);
}
