/**
 * 心跳任务模块头文件
 *
 * @author xingchentye
 * @version 0.3.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_HEARTBEAT_TASK_H
#define WISE_DEPOT_HEARTBEAT_TASK_H

/**
 * 心跳任务回调函数
 *
 * @param ctx 上下文 (未使用)
 */
void heartbeat_task_execute(void *ctx);

/**
 * 设置用于心跳认证的 Token
 *
 * @param token 认证 Token (会复制一份)
 */
void heartbeat_set_token(const char *token);

/**
 * 清除心跳 Token
 */
void heartbeat_clear_token(void);

#endif // WISE_DEPOT_HEARTBEAT_TASK_H
