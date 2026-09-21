/**
 * @file device_auth.c
 * @brief 设备注册与令牌管理（P4-10 批次2b 从 device_service.c 拆出，只搬不改）。
 *
 * 内容：本机 IP 探测、JSON 取值助手、设备注册、令牌读写、令牌刷新。
 * 共享状态定义在 device_service.c，声明见 application/device_internal.h。
 */

#include "application/device_service.h"
#include "application/device_internal.h"
#include "application/heartbeat_task.h"
#include "application/patrol_service.h"
#include "common/config.h"
#include "common/crypto.h"
#include "common/device_info.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "infrastructure/http_client.h"
#include "common/wd_error.h"
#include <cJSON.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

/* 获取本机内网 IP 地址 */
static void get_local_ip(char *buffer, size_t size) {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        strncpy(buffer, "127.0.0.1", size);
        return;
    }

    // 默认回环
    strncpy(buffer, "127.0.0.1", size);

    // 遍历网卡，优先找非回环的 IPv4 地址
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) continue;

            // 忽略回环接口
            if (strcmp(ifa->ifa_name, "lo") != 0 && strcmp(host, "127.0.0.1") != 0) {
                size_t host_len = strlen(host);
                if (host_len >= size) host_len = size - 1;
                memcpy(buffer, host, host_len);
                buffer[host_len] = '\0';
                break; 
            }
        }
    }

    freeifaddrs(ifaddr);
}

static char *extract_json_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    char *p = strstr(json, search_key);
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    char *start = strchr(p, '"');
    if (!start) return NULL;
    start++;
    char *end = strchr(start, '"');
    if (!end) return NULL;
    size_t len = end - start;
    char *val = xmalloc_try(len + 1);
    if (!val) return NULL; /* P4-06：分配失败返回 NULL，由调用方处理 */
    strncpy(val, start, len);
    val[len] = '\0';
    return val;
}

wd_error_t device_register(void) {
    const wd_config_t *cfg = config_get();
    const device_info_t *info = device_info_get();
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/device/register", cfg->server_url);
    
    char ip_addr[64];
    get_local_ip(ip_addr, sizeof(ip_addr));

    char body[2048];
    snprintf(body, sizeof(body), 
             "{"
             "\"deviceCode\": \"%s\", "
             "\"deviceName\": \"%s\", "
             "\"deviceType\": 2, "
             "\"ipAddress\": \"%s\", "
             "\"remark\": \"OS: %s, Kernel: %s, Ver: %s\""
             "}",
             cfg->device_id, 
             info->model,
             ip_addr,
             info->os_name, 
             info->kernel_ver,
             DEVICE_VERSION);
             
    // Generate Signature
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL)); 
    
    char nonce[32];
    // Use timestamp + rand to ensure uniqueness even if rand() collides
    snprintf(nonce, sizeof(nonce), "%s%d", timestamp, rand());
    
    char query_string[1024];
    snprintf(query_string, sizeof(query_string), "nonce=%s&timestamp=%s", nonce, timestamp);
    
    char string_to_sign[2048];
    const char *uri_path = "/api/device"; 
    snprintf(string_to_sign, sizeof(string_to_sign), "POST\n%s\n%s", uri_path, query_string);
    
    unsigned char hmac_result[32];
    const char *signing_secret = config_signature_secret();
    if (signing_secret == NULL) {
        LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); registration aborted");
        return WD_ERR_STATE;
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
    
    const char *headers[3];
    headers[0] = header_sign;
    headers[1] = header_time;
    headers[2] = header_nonce;
    
    snprintf(url, sizeof(url), "%s%s", cfg->server_url, uri_path);
    
    LOG_INFO("Registering device: %s (Model: %s, OS: %s)", cfg->device_id, info->model, info->os_name);
    
    http_response_t *res = http_post_with_retry(url, body, headers, 3, 10000, 3);
    
    xfree(signature);
    
    if (!res) {
        LOG_ERROR("Registration request failed (Network error)");
        return WD_ERR_CONNECT;
    }
    
    if (res->status_code >= 200 && res->status_code < 300) {
        LOG_INFO("Registration successful (Status: %d)", res->status_code);
        
        char *token = extract_json_string(res->body, "token");
        char *refresh_token = extract_json_string(res->body, "refreshToken");

        if (token) {
            if (g_token) xfree(g_token);
            g_token = token;
            LOG_INFO("Received auth token: %s...", "******"); 
            
            heartbeat_set_token(g_token);
            patrol_service_set_token(g_token);
        } else {
            LOG_WARN("No token found in registration response");
        }
        
        if (refresh_token) {
            if (g_refresh_token) xfree(g_refresh_token);
            g_refresh_token = refresh_token;
            LOG_INFO("Received refresh token");
        } else {
            LOG_WARN("No refresh token found in registration response");
        }
        
        g_reauth_needed = 0;
        
        http_response_free(res);
        return 0;
    } else {
        LOG_ERROR("Registration failed (Status: %d, Body: %s)", res->status_code, res->body ? res->body : "");
        http_response_free(res);
        return WD_ERR_SERVER;
    }
}

char *device_service_get_token(void) {
    return g_token;
}

char *device_service_get_refresh_token(void) {
    return g_refresh_token;
}

void device_service_set_tokens(const char *access_token, const char *refresh_token) {
    if (access_token) {
        if (g_token) xfree(g_token);
        g_token = xstrdup_try(access_token);
        if (!g_token) {
            LOG_ERROR("保存访问令牌失败（内存不足），保持原令牌不变");
            return;
        }
        heartbeat_set_token(g_token);
        patrol_service_set_token(g_token);
    }
    
    if (refresh_token) {
        char *copy = xstrdup_try(refresh_token);
        if (!copy) {
            LOG_ERROR("保存刷新令牌失败（内存不足），保持原令牌不变");
            return;
        }
        if (g_refresh_token) xfree(g_refresh_token);
        g_refresh_token = copy;
    }
}

wd_error_t device_refresh_token(void) {
    const wd_config_t *cfg = config_get();
    
    if (!g_refresh_token) {
        LOG_ERROR("No refresh token available");
        return WD_ERR_STATE;
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/auth/refresh-token", cfg->server_url);
    
    char body[1024];
    snprintf(body, sizeof(body), "{\"refreshToken\": \"%s\"}", g_refresh_token);
    
    LOG_INFO("Refreshing token...");
    
    http_response_t *res = http_post(url, body, NULL, 0);
    
    if (!res) {
        LOG_ERROR("Token refresh request failed (Network error)");
        return WD_ERR_CONNECT;
    }
    
    if (res->status_code >= 200 && res->status_code < 300) {
        LOG_INFO("Token refresh successful (Status: %d)", res->status_code);
        
        char *new_token = extract_json_string(res->body, "token");
        char *new_refresh_token = extract_json_string(res->body, "refreshToken");
        
        if (new_token) {
            if (g_token) xfree(g_token);
            g_token = new_token;
            heartbeat_set_token(g_token);
            patrol_service_set_token(g_token);
            LOG_INFO("New access token received");
        }
        
        if (new_refresh_token) {
            if (g_refresh_token) xfree(g_refresh_token);
            g_refresh_token = new_refresh_token;
            LOG_INFO("New refresh token received");
        }
        
        http_response_free(res);
        return 0;
    } else {
        LOG_ERROR("Token refresh failed (Status: %d, Body: %s)", res->status_code, res->body ? res->body : "");
        http_response_free(res);
        return WD_ERR_SERVER;
    }
}
