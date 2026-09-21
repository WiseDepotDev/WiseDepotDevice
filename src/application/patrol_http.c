/**
 * @file patrol_http.c
 * @brief 巡检任务的下发获取与结果上报（含请求签名，P4-10 批次2 从 patrol_service.c 拆出，仅搬不改）。
 */

#include "application/patrol_internal.h"
#include "application/device_service.h"
#include "common/config.h"
#include "common/crypto.h"
#include "common/envelope.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "infrastructure/http_client.h"
#include "common/wd_error.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
/**
 * 检查响应是否成功。
 *
 * 统一走信封解析（common/envelope.h）：只认 payload.code 与生成器产出的成功码，
 * 不再接受历史上的 "00000"/"0" 等兼容分支（STD-ERR-02：单一错误出口）。
 *
 * @param json 服务端响应报文
 * @return true 表示业务成功
 */
static bool is_response_success(const char *json) {
    wd_envelope_t envelope;
    bool ok;

    if (json == NULL) {
        return false;
    }
    if (envelope_parse(json, &envelope) != WD_ENVELOPE_OK) {
        return false;
    }

    ok = (envelope_is_success(&envelope) != 0);
    if (!ok) {
        LOG_WARN("Response business failed: code=%s, errorCode=%s",
                 envelope_code(&envelope) != NULL ? envelope_code(&envelope) : "(null)",
                 envelope_error_code(&envelope) != NULL ? envelope_error_code(&envelope) : "(null)");
    }
    envelope_free(&envelope);
    return ok;
}

/**
 * 从服务端获取待执行的巡检任务
 */
patrol_task_t *patrol_service_fetch_task(void) {
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return NULL;
    }
    
    const wd_config_t *cfg = config_get();
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/inspection/task?status=pending", 
             cfg->server_url);
    
    LOG_DEBUG("Fetching patrol task from: %s", url);
    
    http_response_t *res = NULL;
    
    if (g_patrol_service.token) {
        char auth_header[1024];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", 
                 g_patrol_service.token);
        const char *headers[] = { auth_header };
        res = http_get(url, headers, 1);
    } else {
        char timestamp[20];
        snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
        
        char nonce[20];
        snprintf(nonce, sizeof(nonce), "%d", rand());
        
        char query_string[1024];
        snprintf(query_string, sizeof(query_string), "nonce=%s&status=pending&timestamp=%s", 
                 nonce, timestamp);
        
        char string_to_sign[2048];
        const char *uri_path = "/api/inspection/task";
        snprintf(string_to_sign, sizeof(string_to_sign), "GET\n%s\n%s", uri_path, query_string);
        
        unsigned char hmac_result[32];
        const char *signing_secret = config_signature_secret();
        if (signing_secret == NULL) {
            LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); task fetch aborted");
            return NULL;
        }
        hmac_sha256(signing_secret, strlen(signing_secret),
                    string_to_sign, strlen(string_to_sign), hmac_result);
        
        size_t sig_len = 0;
        char *signature = base64_encode(hmac_result, 32, &sig_len);
        
        char header_sign[256];
        char header_time[64];
        char header_nonce[64];
        
        snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
        snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
        snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
        
        const char *headers[] = { header_sign, header_time, header_nonce };
        res = http_get(url, headers, 3);
        
        xfree(signature);
    }
    
    if (!res) {
        LOG_ERROR("Failed to fetch patrol task (network error)");
        return NULL;
    }
    
    if (res->status_code == 401 || res->status_code == 403) {
        LOG_WARN("Patrol task fetch unauthorized (Status: %d)", res->status_code);
        patrol_service_clear_token();
        device_trigger_reauth();
        http_response_free(res);
        return NULL;
    }
    
    if (res->status_code < 200 || res->status_code >= 300) {
        LOG_ERROR("Failed to fetch patrol task (Status: %d)", res->status_code);
        http_response_free(res);
        return NULL;
    }
    
    if (!res->body || !is_response_success(res->body)) {
        LOG_DEBUG("No pending patrol task or error response");
        http_response_free(res);
        return NULL;
    }
    
    char *data_start = strstr(res->body, "\"data\"");
    if (!data_start) {
        http_response_free(res);
        return NULL;
    }
    
    data_start = strchr(data_start, '[');
    if (!data_start) {
        http_response_free(res);
        return NULL;
    }
    
    char *task_start = strchr(data_start, '{');
    if (!task_start) {
        http_response_free(res);
        return NULL;
    }
    
    patrol_task_t *task = patrol_task_from_json(task_start);
    http_response_free(res);
    
    if (task) {
        LOG_INFO("Fetched patrol task: %s (%s)", task->id, task->name);
    }
    
    return task;
}

/**
 * 上报任务执行结果到服务端
 */
int patrol_service_report_result(const patrol_task_t *task) {
    if (!task) {
        return WD_ERR_PARAM;
    }
    
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return WD_ERR_STATE;
    }
    
    const wd_config_t *cfg = config_get();
    
    const char *status_str = "PENDING";
    if (task->status == PATROL_TASK_STATUS_RUNNING) {
        status_str = "IN_PROGRESS";
    } else if (task->status == PATROL_TASK_STATUS_COMPLETED) {
        status_str = "COMPLETED";
    } else if (task->status == PATROL_TASK_STATUS_FAILED) {
        // Server might not support FAILED, map to COMPLETED or leave as is if supported
        // Based on InspectionApplicationService.updateTaskStatus, it only handles IN_PROGRESS and COMPLETED.
        // Assuming we should mark it completed (maybe with error log separately) or just ignore if failed.
        // For now, let's map FAILED to COMPLETED to ensure it's not stuck in PENDING, 
        // or we need to add FAILED support on server.
        // Let's stick to COMPLETED for now as the server closes the task.
        status_str = "COMPLETED"; 
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/inspection/task/%s/status?status=%s", 
             cfg->server_url, task->id, status_str);
    
    LOG_DEBUG("Updating task status: %s", url);
    
    http_response_t *res = NULL;
    
    if (g_patrol_service.token) {
        char auth_header[1024];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", 
                 g_patrol_service.token);
        const char *headers[] = { auth_header };
        res = http_put(url, NULL, headers, 1);
    } else {
        char timestamp[20];
        snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
        
        char nonce[20];
        snprintf(nonce, sizeof(nonce), "%d", rand());
        
        char query_string[1024];
        snprintf(query_string, sizeof(query_string), "nonce=%s&status=%s&timestamp=%s", 
                 nonce, status_str, timestamp);
        
        char string_to_sign[2048];
        char uri_path[256];
        snprintf(uri_path, sizeof(uri_path), "/api/inspection/task/%s/status", task->id);
        snprintf(string_to_sign, sizeof(string_to_sign), "PUT\n%s\n%s", uri_path, query_string);
        
        unsigned char hmac_result[32];
        const char *signing_secret = config_signature_secret();
        if (signing_secret == NULL) {
            LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); result report aborted");
            return WD_ERR_STATE;
        }
        hmac_sha256(signing_secret, strlen(signing_secret),
                    string_to_sign, strlen(string_to_sign), hmac_result);
        
        size_t sig_len = 0;
        char *signature = base64_encode(hmac_result, 32, &sig_len);
        
        char header_sign[256];
        char header_time[64];
        char header_nonce[64];
        
        snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
        snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
        snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
        
        const char *headers[] = { header_sign, header_time, header_nonce };
        res = http_put(url, NULL, headers, 3);
        
        xfree(signature);
    }
    
    if (!res) {
        LOG_ERROR("Failed to update task status (network error)");
        return WD_ERR_CONNECT;
    }
    
    if (res->status_code == 401 || res->status_code == 403) {
        LOG_WARN("scheduler_task_t status update unauthorized (Status: %d)", res->status_code);
        patrol_service_clear_token();
        device_trigger_reauth();
        http_response_free(res);
        return WD_ERR_AUTH;
    }
    
    bool success = (res->status_code >= 200 && res->status_code < 300);
    
    if (success) {
        LOG_INFO("Patrol task result reported: %s (status: %s)", task->id, status_str);
        http_response_free(res);
        return 0;
    } else {
        LOG_ERROR("Failed to report patrol task result (Status: %d, StatusStr: %s)", res->status_code, status_str);
        http_response_free(res);
        return WD_ERR_SERVER;
    }
}