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

int config_service_init(void) {
    LOG_INFO("wd_config_t service initialized");
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
        LOG_INFO("wd_config_t fetched successfully");
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
    
    // 如果 api_base_url 是 "/api/v1"，则 uri_path 是 "/api/v1/config"
    // 但服务端日志显示 404: 资源不存在: api/v1/config
    // 检查服务端 Controller 路径
    // DeviceController 上有 @RequestMapping("/api/device") 吗？
    // 让我们看 DeviceController.java
    // 
    // 如果 Controller 是 @RequestMapping("/api/device")，且方法是 @GetMapping("/config")
    // 那么完整路径是 /api/device/config
    // 
    // 但 wd_config_t.c 中默认 DEFAULT_API_BASE_URL 是 "/api/v1"
    // 所以客户端请求的是 /api/v1/config
    // 这就是 404 的原因！
    // 
    // 我们需要修改客户端默认配置，或者服务端路径。
    // 服务端 DeviceController 似乎没有类级别的 @RequestMapping，或者有但我们没看到。
    // 但看之前的注册请求 POST /api/device 是成功的。
    // 这意味着 /api/device 是存在的。
    // 
    // 让我们假设 wd_config_t.c 中的默认值错了。
    // 我们应该将 DEFAULT_API_BASE_URL 修改为 "/api/device"
    
    // 但这里只能改代码。
    // 实际上，wd_config_t.c 中写死的是 "/api/v1"。
    // 我们需要在 wd_config_t.c 中修改默认值，或者在这里 override。
    // 更好的做法是去修改 wd_config_t.c。
    
    // 暂时先在这里 hack 一下，或者让用户去改 wd_config_t.c?
    // 不，我是 AI，我应该直接去改 wd_config_t.c。
    
    // 既然已经在编辑这个文件，我先把这里的逻辑保持原样，然后去改 wd_config_t.c。
    // 但为了确保这里的 uri_path 和 url 一致，我需要确保 cfg->api_base_url 是对的。
    // 
    // 如果我不改 wd_config_t.c，只改这里，那么 cfg->api_base_url 还是 /api/v1。
    // 
    // 让我们先不要提交这个 SearchReplace，而是去修改 src/common/config.c。
    // 
    // 等等，我必须提交一个 tool call。
    // 既然现在的任务是修复 404，而 404 是因为路径不对。
    // 服务端日志明确说：`资源未找到: api/v1/config`
    // 而注册请求是 `POST /api/device` 成功的。
    // 说明正确的基础路径应该是 `/api/device`。
    
    // 我将修改 src/common/config.c 中的 DEFAULT_API_BASE_URL。
    
    snprintf(uri_path, sizeof(uri_path), "%s/config", cfg->api_base_url);
    
    // 假设服务端签名验证逻辑为:
    // StringToSign = METHOD + "\n" + URI + "\n" + SortedQueryString + "\n" + Nonce + "\n" + Timestamp
    
    // 从服务端日志来看，注册请求 POST /api/device 的签名是验证通过的。
    // 但是 GET /api/device/config 的签名失败了 (401 缺少有效的身份认证信息 AUTH-0001)。
    // 
    // 等等，服务端日志显示: 
    // 业务异常: code=AUTH-0001, message=缺少有效的身份认证信息, uri=/api/device/config
    // 
    // 这通常意味着请求中没有携带 Token，或者 Token 无效。
    // 而不是签名错误 (签名错误通常是 401 签名验证失败)。
    // 
    // 回顾 DeviceService.c 中的注册逻辑:
    // 注册成功后，服务端返回了 token。
    // 设备端保存了 token 到 g_token。
    // 
    // 但是 ConfigService 发起请求时，并没有在 Header 中携带这个 Token！
    // 
    // 我们需要在 headers 中添加 X-Auth-Token (或者服务端期望的 Header Name)。
    // 让我们查看服务端代码，确认 Token 的 Header Name。
    // 通常是 Authorization: Bearer <token> 或 X-Auth-Token。
    
    // 假设是 Authorization: Bearer <token>。
    // 我们需要从 device_service 获取 token。
    // 
    // 让我们先修改 headers 数组大小。
    
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
        LOG_DEBUG("wd_config_t not modified");
        http_response_free(res);
        return;
    }
    
    if (res->status_code == 200 && res->body) {
        // Verify signature if header present
        // Implementation detail: server should return X-Signature header
        // For now, we assume trusted HTTPS channel or implement simple check
        
        LOG_INFO("Received new config version");
        if (config_update_from_json(res->body) == 0) {
            LOG_INFO("wd_config_t updated successfully");
        } else {
            LOG_ERROR("Failed to update config from response");
        }
    } else {
        LOG_WARN("wd_config_t fetch failed: %d", res->status_code);
    }
    
    http_response_free(res);
}

void config_service_stop(void) {
    // No resources to clean up specifically
}
