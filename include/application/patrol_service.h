/**
 * 巡检服务模块头文件
 * 
 * 负责从服务端获取巡检任务并执行
 * 
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-12
 */

#ifndef WISE_DEPOT_PATROL_SERVICE_H
#define WISE_DEPOT_PATROL_SERVICE_H

#include "domain/patrol_task.h"
#include "common/wd_error.h"
#include <stdbool.h>

/**
 * 巡检服务配置结构体
 */
typedef struct {
    int task_poll_interval;     /**< 任务轮询间隔 (秒) */
    int task_timeout;           /**< 任务执行超时 (秒) */
    bool auto_report_status;    /**< 是否自动上报任务状态 */
} patrol_service_config_t;

/**
 * 初始化巡检服务
 * 
 * @param config 服务配置 (NULL则使用默认配置)
 * @return 0 成功，-1 失败
 */
/**
 * @brief 初始化巡检服务（队列、线程、状态）
 * @param config 配置
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t patrol_service_init(const patrol_service_config_t *config);

/**
 * 从服务端获取待执行的巡检任务
 * 
 * @return 任务结构体指针 (需要调用者释放)，无任务或失败返回NULL
 */
patrol_task_t *patrol_service_fetch_task(void);

/**
 * 执行从服务端获取的巡检任务
 * 
 * @return 0 成功，-1 失败
 */
/**
 * @brief 取出并执行队列中的巡检任务
 * @return 0 成功；负值失败
 */
int patrol_service_execute_task(void);

/**
 * 上报任务执行结果到服务端
 * 
 * @param task 已完成的任务
 * @return 0 成功，-1 失败
 */
/**
 * @brief 上报巡检任务执行结果
 * @param task 巡检任务
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t patrol_service_report_result(const patrol_task_t *task);

/**
 * 巡检服务主循环 (由调度器调用)
 * 
 * @param ctx 上下文 (未使用)
 */
/**
 * @brief 巡检服务主循环（取任务 → 执行 → 上报）
 * @param ctx 参数
 */
void patrol_service_run(void *ctx);

/**
 * 设置认证Token
 * 
 * @param token 认证Token (会复制一份)
 */
/**
 * @brief 设置巡检上报所用令牌
 * @param token 访问令牌
 */
void patrol_service_set_token(const char *token);

/**
 * 清除认证Token
 */
/**
 * @brief 清除巡检上报令牌
 */
void patrol_service_clear_token(void);

/**
 * 释放巡检服务资源
 */
/**
 * @brief 停止巡检服务并回收线程与队列
 */
void patrol_service_cleanup(void);

/**
 * 获取默认巡检服务配置
 * 
 * @return 默认配置结构体
 */
patrol_service_config_t patrol_service_get_default_config(void);

/**
 * 检查是否有正在执行的任务
 * 
 * @return true 有任务正在执行，false 无任务执行
 */
/**
 * @brief 取巡检服务是否正在执行任务
 * @return true = 正在执行任务
 */
bool patrol_service_is_busy(void);

/**
 * 获取当前执行的任务
 * 
 * @return 当前任务指针 (只读)，无任务返回NULL
 */
const patrol_task_t *patrol_service_get_current_task(void);

/**
 * 启动指定的巡检任务
 * 
 * @param task 任务指针 (该函数会接管 task 的内存所有权)
 * @return 0 成功，-1 失败 (任务正在执行或 task 为 NULL)
 */
/**
 * @brief 把巡检任务投入执行队列
 * @param task 巡检任务
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t patrol_service_start_task(patrol_task_t *task);

#endif // WISE_DEPOT_PATROL_SERVICE_H
