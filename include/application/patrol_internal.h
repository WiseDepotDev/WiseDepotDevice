/**
 * @file patrol_internal.h
 * @brief 巡检服务内部共享定义（P4-10 批次2：随 patrol_service.c 拆分而引入）。
 *
 * 拆分后的分工：
 * - patrol_service.c：服务状态与生命周期（init/cleanup/队列/线程）
 * - patrol_action.c ：单个巡检动作的执行与扫描累积
 * - patrol_http.c   ：取任务与上报结果（含请求签名）
 *
 * 这是**模块内部**头文件，外部只应包含 application/patrol_service.h。
 */

#ifndef WISE_DEPOT_PATROL_INTERNAL_H
#define WISE_DEPOT_PATROL_INTERNAL_H

#include "application/patrol_service.h"
#include "domain/patrol_task.h"
#include "domain/tag.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/* ===== 常量 ===== */
/** 单次巡检中累积的 RFID 标签缓冲区（按 EPC 去重） */
#define PATROL_MAX_ACCUMULATED_TAGS 2000
#define PATROL_SCAN_BATCH_SIZE      256
#define MAX_TASK_QUEUE_SIZE         10

/** 单次巡检的扫描累积上下文 */
typedef struct {
    rfid_tag_t *tags;
    size_t count;
    size_t capacity;
} patrol_scan_context_t;

/* ===== 服务状态（唯一定义在 patrol_service.c） ===== */
typedef struct {
    patrol_service_config_t config;     /**< 服务配置 */
    char *token;                    /**< 认证Token */
    patrol_task_t *current_task;       /**< 当前执行的任务（归执行线程所有，cleanup 不得释放） */
    patrol_task_t *task_queue[MAX_TASK_QUEUE_SIZE]; /**< 任务队列 */
    int queue_head;                 /**< 队列头 */
    int queue_tail;                 /**< 队列尾 */
    int queue_count;                /**< 队列任务数量 */
    pthread_mutex_t queue_mutex;    /**< 队列互斥锁（同时保护 worker_running/shutting_down） */
    pthread_cond_t worker_done;     /**< 执行线程结束通知（P4-07） */
    bool worker_running;            /**< 是否有执行线程在跑 */
    bool shutting_down;             /**< 关闭中：不再接受/启动新任务 */
    bool initialized;               /**< 初始化标志 */
} patrol_service_state_t;

extern patrol_service_state_t g_patrol_service;

/* ===== 跨文件内部接口 ===== */
/** 单个巡检动作的执行入口（原 static，拆分后跨文件可见；作为回调传给 patrol_task_execute） */
int patrol_action_callback(const patrol_task_t *task, uint8_t action_index, void *context);

#endif // WISE_DEPOT_PATROL_INTERNAL_H