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
#include "application/patrol_internal.h"
#include "application/device_service.h"
#include "application/rfid_service.h"
#include "application/motor_service.h"
#include "domain/inventory_manager.h"
#include "domain/tag.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "infrastructure/http_client.h"
#include "common/crypto.h"
#include "common/envelope.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For usleep
#include <time.h>
#include <pthread.h>

/* 常量与扫描上下文定义见 application/patrol_internal.h（P4-10 批次2） */


/* 签名密钥：统一走 config_signature_secret()（环境变量 / 配置文件注入，源码内无默认值，P4-04） */


// Forward declaration
static void check_and_start_next_task(void);
static void *patrol_task_thread_func(void *arg);


/* 全局状态（结构体定义见 application/patrol_internal.h） */
patrol_service_state_t g_patrol_service = {.worker_done = PTHREAD_COND_INITIALIZER};

/**
 * 获取默认巡检服务配置
 */
patrol_service_config_t patrol_service_get_default_config(void) {
    patrol_service_config_t config = {
        .task_poll_interval = 10,   /* 10秒轮询一次 */
        .task_timeout = 300,        /* 5分钟超时 */
        .auto_report_status = true  /* 自动上报状态 */
    };
    return config;
}

/**
 * 初始化巡检服务
 */
int patrol_service_init(const patrol_service_config_t *config) {
    if (g_patrol_service.initialized) {
        LOG_WARN("Patrol service already initialized");
        return 0;
    }
    
    if (config) {
        memcpy(&g_patrol_service.config, config, sizeof(patrol_service_config_t));
    } else {
        g_patrol_service.config = patrol_service_get_default_config();
    }
    
    g_patrol_service.token = NULL;
    g_patrol_service.current_task = NULL;
    g_patrol_service.queue_head = 0;
    g_patrol_service.queue_tail = 0;
    g_patrol_service.queue_count = 0;
    /* P4-07：重新初始化时复位生命周期标志（worker_done 条件变量可复用，无需重建） */
    g_patrol_service.worker_running = false;
    g_patrol_service.shutting_down = false;
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
    if (token) {
        /* P4-06：先复制成功再替换，失败时保持原令牌不变 */
        char *copy = xstrdup_try(token);
        if (!copy) {
            LOG_ERROR("保存巡检服务令牌失败（内存不足），保持原令牌不变");
            return;
        }
        if (g_patrol_service.token) xfree(g_patrol_service.token);
        g_patrol_service.token = copy;
        return;
    }
    if (g_patrol_service.token) {
        xfree(g_patrol_service.token);
        g_patrol_service.token = NULL;
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
 * 巡检任务执行线程
 * 流程：1) 获取仓库预期库存 2) 执行动作（移动/扫描时仅累积 RFID）3) 巡检结束后与预期比对并上传报告 4) 上报任务完成
 */
static void *patrol_task_thread_func(void *arg) {
    patrol_task_t *task = (patrol_task_t *)arg;
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
        .tags = (rfid_tag_t *)xcalloc_try(PATROL_MAX_ACCUMULATED_TAGS, sizeof(rfid_tag_t)),
        .count = 0,
        .capacity = PATROL_MAX_ACCUMULATED_TAGS
    };
    if (!scan_ctx.tags) {
        LOG_ERROR("分配巡检扫描缓冲区失败（内存不足），本次巡检中止");
        return NULL;
    }
    
    // 3. 上报开始执行 (Status: IN_PROGRESS)
    if (g_patrol_service.config.auto_report_status) {
        patrol_task_status_t original_status = task->status;
        task->status = PATROL_TASK_STATUS_RUNNING;
        patrol_service_report_result(task);
        task->status = original_status;
    }
    
    // 4. 执行巡检动作（移动/扫描时只做 rfid_service_scan_only 并累积到 scan_ctx）
    int result = patrol_task_execute(task, patrol_action_callback, &scan_ctx);
    
    /* P4-07：不要覆盖执行阶段得出的状态——被取消/失败的任务必须如实上报，
     * 原实现无条件写成 COMPLETED，导致"取消后仍显示已完成"。 */
    if (result == 0 && task->status == PATROL_TASK_STATUS_RUNNING) {
        task->status = PATROL_TASK_STATUS_COMPLETED;
    }
    
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
    g_patrol_service.worker_running = false;
    /* 通知 cleanup：执行线程已停止持有任务（P4-07：避免 cleanup 释放正在使用的任务） */
    pthread_cond_broadcast(&g_patrol_service.worker_done);
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    
    check_and_start_next_task();
    
    return NULL;
}

/**
 * 启动指定的巡检任务（如果当前有任务在运行，则加入队列）
 *
 * 所有权契约（P4-07）：
 * - 返回 0：任务所有权**移交给服务**（服务负责执行后释放，或在 cleanup 时释放队列中的任务）；
 * - 返回 -1：服务**不接管**，调用方仍需自行释放 task（避免"失败后任务丢失/泄漏"）。
 */
int patrol_service_start_task(patrol_task_t *task) {
    if (!g_patrol_service.initialized) {
        LOG_ERROR("Patrol service not initialized");
        return -1;
    }
    
    if (!task) return -1;
    
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    
    if (g_patrol_service.shutting_down) {
        LOG_WARN("Patrol service is shutting down, rejecting task: %s", task->id);
        pthread_mutex_unlock(&g_patrol_service.queue_mutex);
        return -1;
    }
    
    // Check if current task is running
    if (g_patrol_service.current_task == NULL) {
        g_patrol_service.current_task = task;
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, patrol_task_thread_func, task) != 0) {
            LOG_ERROR("Failed to create patrol task thread");
            g_patrol_service.current_task = NULL;
            pthread_mutex_unlock(&g_patrol_service.queue_mutex);
            return -1; /* 所有权仍属调用方 */
        }
        g_patrol_service.worker_running = true;
        pthread_detach(thread_id);
    } else {
        // Add to queue
        if (g_patrol_service.queue_count >= MAX_TASK_QUEUE_SIZE) {
            LOG_WARN("scheduler_task_t queue is full, rejecting task: %s", task->id);
            pthread_mutex_unlock(&g_patrol_service.queue_mutex);
            return -1;
        }
        
        g_patrol_service.task_queue[g_patrol_service.queue_tail] = task;
        g_patrol_service.queue_tail = (g_patrol_service.queue_tail + 1) % MAX_TASK_QUEUE_SIZE;
        g_patrol_service.queue_count++;
        LOG_INFO("scheduler_task_t queued: %s (Queue size: %d)", task->id, g_patrol_service.queue_count);
    }
    
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    return 0;
}

static void check_and_start_next_task(void) {
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    
    if (g_patrol_service.shutting_down) {
        pthread_mutex_unlock(&g_patrol_service.queue_mutex);
        return;
    }
    
    if (g_patrol_service.current_task == NULL && g_patrol_service.queue_count > 0) {
        patrol_task_t *next_task = g_patrol_service.task_queue[g_patrol_service.queue_head];
        // Move head pointer
        g_patrol_service.queue_head = (g_patrol_service.queue_head + 1) % MAX_TASK_QUEUE_SIZE;
        g_patrol_service.queue_count--;
        
        g_patrol_service.current_task = next_task;
        
        LOG_INFO("Starting next task from queue: %s (Queue size: %d)", next_task->id, g_patrol_service.queue_count);
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, patrol_task_thread_func, next_task) != 0) {
            LOG_ERROR("Failed to create patrol task thread");
            g_patrol_service.current_task = NULL;
            /* 该任务已从队列出队 → 服务持有所有权，启动失败时在此释放 */
            patrol_task_free(next_task);
        } else {
            g_patrol_service.worker_running = true;
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
    
    patrol_task_t *task = patrol_service_fetch_task();
    if (!task) {
        return 0;
    }
    
    /* P4-07 所有权契约：start_task 失败时服务不接管，调用方负责释放 */
    int rc = patrol_service_start_task(task);
    if (rc != 0) {
        patrol_task_free(task);
    }
    return rc;
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
 *
 * P4-07 生命周期契约：
 * 1. 先置 shutting_down，阻止新任务进入；
 * 2. 取消当前任务（每任务标志），并**等待执行线程真正结束**（worker_done 条件变量）；
 * 3. 当前任务由执行线程自己释放（cleanup 绝不 free 正在使用的 task —— 原实现就是 use-after-free）；
 * 4. 队列中尚未执行的任务全部释放。
 */
void patrol_service_cleanup(void) {
    if (!g_patrol_service.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    g_patrol_service.shutting_down = true;
    patrol_task_t *running = g_patrol_service.current_task;
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    
    if (running) {
        patrol_task_cancel(running);
        
        /* 等待执行线程结束（最多 30s，正常情况会很快返回） */
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 30;
        
        pthread_mutex_lock(&g_patrol_service.queue_mutex);
        while (g_patrol_service.worker_running) {
            if (pthread_cond_timedwait(&g_patrol_service.worker_done, &g_patrol_service.queue_mutex, &deadline) != 0) {
                LOG_ERROR("等待巡检线程结束超时，放弃等待（避免释放正在使用的任务）");
                break;
            }
        }
        pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    }
    
    /* 释放队列中未执行的任务 */
    pthread_mutex_lock(&g_patrol_service.queue_mutex);
    while (g_patrol_service.queue_count > 0) {
        patrol_task_t *queued = g_patrol_service.task_queue[g_patrol_service.queue_head];
        g_patrol_service.task_queue[g_patrol_service.queue_head] = NULL;
        g_patrol_service.queue_head = (g_patrol_service.queue_head + 1) % MAX_TASK_QUEUE_SIZE;
        g_patrol_service.queue_count--;
        if (queued) {
            patrol_task_free(queued);
        }
    }
    pthread_mutex_unlock(&g_patrol_service.queue_mutex);
    
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
const patrol_task_t *patrol_service_get_current_task(void) {
    return g_patrol_service.current_task;
}
