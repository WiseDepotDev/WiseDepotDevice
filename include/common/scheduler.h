/**
 * 任务调度器模块头文件
 *
 * @author xingchentye
 * @version 0.3.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_SCHEDULER_H
#define WISE_DEPOT_SCHEDULER_H

#include <stddef.h>
#include <stdbool.h>

/**
 * 任务回调函数类型
 *
 * @param context 任务上下文指针
 */
typedef void (*scheduler_task_callback_t)(void *context);

/**
 * 任务结构体
 */
typedef struct scheduler_task_t {
    char *name;              /**< 任务名称 */
    scheduler_task_callback_t callback;   /**< 回调函数 */
    void *context;           /**< 上下文数据 */
    unsigned int interval_ms;/**< 执行间隔 (毫秒) */
    unsigned long long last_run; /**< 上次执行时间戳 (毫秒) */
    struct scheduler_task_t *next;       /**< 链表指针 */
} scheduler_task_t;

/**
 * 初始化调度器
 *
 * @return 0 成功，-1 失败
 */
int scheduler_init(void);

/**
 * 添加任务
 *
 * @param name 任务名称 (内部会复制副本)
 * @param callback 回调函数
 * @param context 上下文指针 (由调用者管理生命周期)
 * @param interval_ms 执行间隔 (毫秒)
 * @return 0 成功，-1 失败
 */
int scheduler_add_task(const char *name, scheduler_task_callback_t callback, void *context, unsigned int interval_ms);

/**
 * 运行调度器 (阻塞直到 scheduler_stop 被调用)
 *
 * @param resolution_ms 调度器检查间隔 (毫秒)，建议 100-1000
 */
void scheduler_run(unsigned int resolution_ms);

/**
 * 停止调度器
 */
void scheduler_stop(void);

/**
 * 销毁调度器并释放资源
 */
void scheduler_destroy(void);

#endif // WISE_DEPOT_SCHEDULER_H
