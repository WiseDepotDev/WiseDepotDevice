/**
 * 巡检服务模块实现
 * 
 * 负责从服务端获取巡检任务并执行
 * 
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-12
 */

#include "application/patrol_service.h"
#include "application/device_service.h"
#include "application/rfid_service.h"
#include "application/motor_service.h"
#include "domain/inventory_manager.h"
#include "infrastructure/rfid_driver.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "infrastructure/http_client.h"
#include "common/crypto.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For usleep
#include <time.h>
#include <pthread.h>

/** 单次巡检中累积的 RFID 标签缓冲区（按 EPC 去重） */
#define PATROL_MAX_ACCUMULATED_TAGS 2000
#define PATROL_SCAN_BATCH_SIZE      256

typedef struct {
    rfid_tag_t *tags;
    size_t count;
    size_t capacity;
} patrol_scan_context_t;

/* 签名密钥 (应与 device_service.c 共享或从配置读取) */
#define SIGNATURE_SECRET "wise-depot-api-signature-secret-key-2024"

#define MAX_TASK_QUEUE_SIZE 10

// Forward declaration
static void check_and_start_next_task(void);
static void *patrol_task_thread_func(void *arg);

/** 判断两个标签的 EPC 是否相同 */
static bool tag_epc_equal(const rfid_tag_t *a, const rfid_tag_t *b) {
    if (a->epc_len != b->epc_len) return false;
    return memcmp(a->epc, b->epc, a->epc_len) == 0;
}

/** 将本批扫描到的标签合并到巡检上下文中（按 EPC 去重） */
static void merge_scan_into_context(patrol_scan_context_t *ctx, const rfid_tag_t *batch, int batch_count) {
    for (int i = 0; i < batch_count && ctx->count < ctx->capacity; i++) {
        bool found = false;
        for (size_t k = 0; k < ctx->count; k++) {
            if (tag_epc_equal(&ctx->tags[k], &batch[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            memcpy(&ctx->tags[ctx->count], &batch[i], sizeof(rfid_tag_t));
            ctx->count++;
        }
    }
}

static int patrol_action_callback(const PatrolTask *task, uint8_t action_index, void *context) {
    patrol_scan_context_t *scan_ctx = (patrol_scan_context_t *)context;
    if (action_index >= task->action_count) return -1;
    const PatrolAction *action = &task->actions[action_index];
    
    // 移动动作：移动过程中仅做 RFID 扫描并累积到 context，不拉取库存、不上传
    if (action->type == PATROL_ACTION_MOVE_FORWARD || 
        action->type == PATROL_ACTION_MOVE_BACKWARD ||
        action->type == PATROL_ACTION_MOVE_LEFT ||
        action->type == PATROL_ACTION_MOVE_RIGHT ||
        action->type == PATROL_ACTION_TURN_LEFT ||
        action->type == PATROL_ACTION_TURN_RIGHT) {
        
        LOG_INFO("Executing move action with RFID scanning (accumulate only): %s", 
                 patrol_action_type_to_string(action->type));
        
        MoveDirection dir;
        switch(action->type) {
            case PATROL_ACTION_MOVE_FORWARD: dir = MOVE_FORWARD; break;
            case PATROL_ACTION_MOVE_BACKWARD: dir = MOVE_BACKWARD; break;
            case PATROL_ACTION_MOVE_LEFT: dir = MOVE_LEFT; break;
            case PATROL_ACTION_MOVE_RIGHT: dir = MOVE_RIGHT; break;
            case PATROL_ACTION_TURN_LEFT: dir = MOVE_TURN_LEFT; break;
            case PATROL_ACTION_TURN_RIGHT: dir = MOVE_TURN_RIGHT; break;
            default: dir = MOVE_FORWARD; break; 
        }
        
        if (motor_move_async(dir, action->speed) < 0) {
            LOG_ERROR("Failed to start motor movement");
            return -1;
        }
        
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        long elapsed_ms = 0;
        rfid_tag_t *batch = (rfid_tag_t *)xcalloc(PATROL_SCAN_BATCH_SIZE, sizeof(rfid_tag_t));
        
        while (elapsed_ms < action->duration_ms) {
            int n = rfid_service_scan_only(batch, PATROL_SCAN_BATCH_SIZE);
            if (n > 0 && scan_ctx) {
                merge_scan_into_context(scan_ctx, batch, n);
            }
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 + 
                         (current_time.tv_nsec - start_time.tv_nsec) / 1000000;
            
            if (elapsed_ms % 1000 < 100) {
                int progress = (int)((elapsed_ms * 100) / action->duration_ms);
                if (progress > 100) progress = 100;
                char url[1024];
                snprintf(url, sizeof(url), "%s/api/inspection/task/%s/progress?progress=%d", 
                         config_get()->server_url, task->id, progress);
                char timestamp[20];
                snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
                char nonce[32];
                snprintf(nonce, sizeof(nonce), "%s%d", timestamp, rand());
                char uri_path[256];
                snprintf(uri_path, sizeof(uri_path), "/api/inspection/task/%s/progress", task->id);
                char query_string[256];
                snprintf(query_string, sizeof(query_string), "nonce=%s&progress=%d&timestamp=%s", nonce, progress, timestamp);
                char string_to_sign[2048];
                snprintf(string_to_sign, sizeof(string_to_sign), "PUT\n%s\n%s", uri_path, query_string);
                unsigned char hmac_result[32];
                hmac_sha256(SIGNATURE_SECRET, strlen(SIGNATURE_SECRET), string_to_sign, strlen(string_to_sign), hmac_result);
                size_t sig_len = 0;
                char *signature = base64_encode(hmac_result, 32, &sig_len);
                char header_sign[256], header_time[64], header_nonce[64];
                snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
                snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
                snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
                const char *headers[] = { header_sign, header_time, header_nonce };
                HttpResponse *res = http_put(url, NULL, headers, 3);
                if (res) {
                    if (res->status_code >= 200 && res->status_code < 300) {
                        LOG_DEBUG("Progress updated: %d%%", progress);
                    } else {
                        LOG_ERROR("Progress update failed (Status: %d)", res->status_code);
                    }
                    http_response_free(res);
                }
                xfree(signature);
            }
            
            usleep(100000); // 100ms 间隔再扫
        }
        
        xfree(batch);
        motor_service_stop_all();
        return 1;
    }
    
    if (action->type == PATROL_ACTION_RFID_SCAN) {
        LOG_INFO("Executing RFID Scan Action (accumulate only)");
        rfid_tag_t *batch = (rfid_tag_t *)xcalloc(PATROL_SCAN_BATCH_SIZE, sizeof(rfid_tag_t));
        int n = rfid_service_scan_only(batch, PATROL_SCAN_BATCH_SIZE);
        if (n > 0 && scan_ctx) {
            merge_scan_into_context(scan_ctx, batch, n);
        }
        xfree(batch);
        return 1;
    }
    return 0;
}

/* 全局状态 */
static struct {
    PatrolServiceConfig config;     /**< 服务配置 */
    char *token;                    /**< 认证Token */
    PatrolTask *current_task;       /**< 当前执行的任务 */
    PatrolTask *task_queue[MAX_TASK_QUEUE_SIZE]; /**< 任务队列 */
    int queue_head;                 /**< 队列头 */
    int queue_tail;                 /**< 队列尾 */
    int queue_count;                /**< 队列任务数量 */
    pthread_mutex_t queue_mutex;    /**< 队列互斥锁 */
    bool initialized;               /**< 初始化标志 */
} g_patrol_service = {0};

/**
 * 获取默认巡检服务配置
 */
PatrolServiceConfig patrol_service_get_default_config(void) {
    PatrolServiceConfig config = {
        .task_poll_interval = 10,   /* 10秒轮询一次 */
        .task_timeout = 300,        /* 5分钟超时 */
        .auto_report_status = true  /* 自动上报状态 */
    };
    return config;
}

/**
 * 初始化巡检服务
 */
int patrol_service_init(const PatrolServiceConfig *config) {
    if (g_patrol_service.initialized) {
        LOG_WARN("Patrol service already initialized");
        return 0;
    }
    
    if (config) {
        memcpy(&g_patrol_service.config, config, sizeof(PatrolServiceConfig));
    } else {
        g_patrol_service.config = patrol_service_get_default_config();
    }
    
    g_patrol_service.token = NULL;
    g_patrol_service.current_task = NULL;
    g_patrol_service.queue_head = 0;
    g_patrol_service.queue_tail = 0;
    g_patrol_service.queue_count = 0;
    pthread_mutex_init(&g_patrol_service.queue_mutex, NULL);
    g_patrol_service.initialized = true;
    
    LOG_INFO("Patrol service initialized (poll interval: %ds, timeout: %ds)",
             g_patrol_service.config.task_poll_interval,
             g_patrol_service.config.task_timeout);
    
    return 0;
}

/**
 * 设置认证Token
 */
void patrol_service_set_token(const char *token) {
    if (g_patrol_service.token) {
        xfree(g_patrol_service.token);
        g_patrol_service.token = NULL;
    }
    if (token) {
        g_patrol_service.token = xstrdup(token);
    }
}

/**
 * 清除认证Token
 */
void patrol_service_clear_token(void) {
    if (g_patrol_service.token) {
        xfree(g_patrol_service.token);
        g_patrol_service.token = NULL;
    }
}

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
PatrolTask *patrol_service_fetch_task(void) {
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return NULL;
    }
    
    const Config *cfg = config_get();
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/inspection/task?status=pending", 
             cfg->server_url);
    
    LOG_DEBUG("Fetching patrol task from: %s", url);
    
    HttpResponse *res = NULL;
    
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
        hmac_sha256(SIGNATURE_SECRET, strlen(SIGNATURE_SECRET), 
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
    
    PatrolTask *task = patrol_task_from_json(task_start);
    http_response_free(res);
    
    if (task) {
        LOG_INFO("Fetched patrol task: %s (%s)", task->id, task->name);
    }
    
    return task;
}

/**
 * 上报任务执行结果到服务端
 */
int patrol_service_report_result(const PatrolTask *task) {
    if (!task) {
        return -1;
    }
    
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return -1;
    }
    
    const Config *cfg = config_get();
    
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
    
    HttpResponse *res = NULL;
    
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
        hmac_sha256(SIGNATURE_SECRET, strlen(SIGNATURE_SECRET), 
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
        return -1;
    }
    
    if (res->status_code == 401 || res->status_code == 403) {
        LOG_WARN("Task status update unauthorized (Status: %d)", res->status_code);
        patrol_service_clear_token();
        device_trigger_reauth();
        http_response_free(res);
        return -1;
    }
    
    bool success = (res->status_code >= 200 && res->status_code < 300);
    
    if (success) {
        LOG_INFO("Patrol task result reported: %s (status: %s)", task->id, status_str);
        http_response_free(res);
        return 0;
    } else {
        LOG_ERROR("Failed to report patrol task result (Status: %d, StatusStr: %s)", res->status_code, status_str);
        http_response_free(res);
        return -1;
    }
}

/**
 * 巡检任务执行线程
 * 流程：1) 获取仓库预期库存 2) 执行动作（移动/扫描时仅累积 RFID）3) 巡检结束后与预期比对并上传报告 4) 上报任务完成
 */
static void *patrol_task_thread_func(void *arg) {
    PatrolTask *task = (PatrolTask *)arg;
    if (!task) return NULL;

    LOG_INFO("Patrol task execution thread started: %s", task->id);
    
    // 1. 巡检开始：从服务端获取仓库预期库存（只拉一次，用于后续比对）
    if (rfid_service_fetch_expected_inventory() != 0) {
        LOG_WARN("Failed to fetch expected inventory, patrol will compare with empty/cached list");
    } else {
        LOG_INFO("Expected inventory loaded for patrol task: %s", task->id);
    }
    
    // 2. 初始化本次巡检的 RFID 累积上下文（按 EPC 去重）
    patrol_scan_context_t scan_ctx = {
        .tags = (rfid_tag_t *)xcalloc(PATROL_MAX_ACCUMULATED_TAGS, sizeof(rfid_tag_t)),
        .count = 0,
        .capacity = PATROL_MAX_ACCUMULATED_TAGS
    };
    
    // 3. 上报开始执行 (Status: IN_PROGRESS)
    if (g_patrol_service.config.auto_report_status) {
        PatrolTaskStatus original_status = task->status;
        task->status = PATROL_TASK_STATUS_RUNNING;
        patrol_service_report_result(task);
        task->status = original_status;
    }
    
    // 4. 执行巡检动作（移动/扫描时只做 rfid_service_scan_only 并累积到 scan_ctx）
    int result = patrol_task_execute(task, patrol_action_callback, &scan_ctx);
    
    // 设置任务状态为已完成
    task->status = PATROL_TASK_STATUS_COMPLETED;
    
    // 5. 巡检结束：用累积的 RFID 与预期库存比对，生成报告并上传服务端（服务端分发给手机展示差异列表）
    inventory_report_t *report = inventory_process_scan(scan_ctx.tags, scan_ctx.count);
    if (report) {
        rfid_service_upload_inspection_report(report, task->id);
        LOG_INFO("Patrol report uploaded: task=%s, scanned=%zu, differences=%zu", 
                 task->id, (size_t)scan_ctx.count, report->diff_count);
        inventory_free_report(report);
    }
    xfree(scan_ctx.tags);
    
    // 6. 上报任务完成 (Status: COMPLETED/FAILED)
    if (g_patrol_service.config.auto_report_status) {
        patrol_service_report_result(task);
    }
    
    LOG_INFO("Patrol task completed: %s (result: %d)", task->id, result);
    
    patrol_task_free(task);
    
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    g_patrol_service.current_task = NULL;
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    
    check_and_start_next_task();
    
    return NULL;
}

/**
 * 启动指定的巡检任务（如果当前有任务在运行，则加入队列）
 */
int patrol_service_start_task(PatrolTask *task) {
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return -1;
    }
    
    if (!task) return -1;
    
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    
    // Check if current task is running
    if (g_patrol_service.current_task == NULL) {
        g_patrol_service.current_task = task;
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, patrol_task_thread_func, task) != 0) {
            LOG_ERROR("Failed to create patrol task thread");
            g_patrol_service.current_task = NULL;
            pthread_mutex_unlock(&g_patrol_service.queue_mutex);
            return -1;
        }
        pthread_detach(thread_id);
    } else {
        // Add to queue
        if (g_patrol_service.queue_count >= MAX_TASK_QUEUE_SIZE) {
            LOG_WARN("Task queue is full, rejecting task: %s", task->id);
            pthread_mutex_unlock(&g_patrol_service.queue_mutex);
            return -1;
        }
        
        g_patrol_service.task_queue[g_patrol_service.queue_tail] = task;
        g_patrol_service.queue_tail = (g_patrol_service.queue_tail + 1) % MAX_TASK_QUEUE_SIZE;
        g_patrol_service.queue_count++;
        LOG_INFO("Task queued: %s (Queue size: %d)", task->id, g_patrol_service.queue_count);
    }
    
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    return 0;
}

static void check_and_start_next_task(void) {
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    
    if (g_patrol_service.current_task == NULL && g_patrol_service.queue_count > 0) {
        PatrolTask *next_task = g_patrol_service.task_queue[g_patrol_service.queue_head];
        // Move head pointer
        g_patrol_service.queue_head = (g_patrol_service.queue_head + 1) % MAX_TASK_QUEUE_SIZE;
        g_patrol_service.queue_count--;
        
        g_patrol_service.current_task = next_task;
        
        LOG_INFO("Starting next task from queue: %s (Queue size: %d)", next_task->id, g_patrol_service.queue_count);
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, patrol_task_thread_func, next_task) != 0) {
            LOG_ERROR("Failed to create patrol task thread");
            g_patrol_service.current_task = NULL;
            // If failed to start, we lost the task. In robust system, retry or put back.
            // For now, just log and continue.
            patrol_task_free(next_task);
        } else {
            pthread_detach(thread_id);
        }
    }
    
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
}

/**
 * 执行从服务端获取的巡检任务
 */
int patrol_service_execute_task(void) {
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return -1;
    }
    
    if (g_patrol_service.current_task) {
        LOG_WARN("Another task is already running");
        return -1;
    }
    
    PatrolTask *task = patrol_service_fetch_task();
    if (!task) {
        return 0;
    }
    
    return patrol_service_start_task(task);
}

/**
 * 巡检服务主循环 (由调度器调用)
 */
void patrol_service_run(void *ctx) {
    (void)ctx;
    
    if (!g_patrol_service.initialized) {
        return;
    }
    
    if (g_patrol_service.current_task) {
        LOG_DEBUG("Patrol service busy, skipping task fetch");
        return;
    }
    
    patrol_service_execute_task();
}

/**
 * 释放巡检服务资源
 */
void patrol_service_cleanup(void) {
    if (!g_patrol_service.initialized) {
        return;
    }
    
    if (g_patrol_service.current_task) {
        patrol_task_cancel(g_patrol_service.current_task);
        patrol_task_free(g_patrol_service.current_task);
        g_patrol_service.current_task = NULL;
    }
    
    patrol_service_clear_token();
    g_patrol_service.initialized = false;
    
    LOG_INFO("Patrol service cleanup completed");
}

/**
 * 检查是否有正在执行的任务
 */
bool patrol_service_is_busy(void) {
    return g_patrol_service.current_task != NULL;
}

/**
 * 获取当前执行的任务
 */
const PatrolTask *patrol_service_get_current_task(void) {
    return g_patrol_service.current_task;
}
