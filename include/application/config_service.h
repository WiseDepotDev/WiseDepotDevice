/**
 * 配置服务模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-13
 */

#ifndef WISE_DEPOT_CONFIG_SERVICE_H
#define WISE_DEPOT_CONFIG_SERVICE_H

/**
 * 初始化配置服务
 *
 * @return 0 成功，-1 失败
 */
int config_service_init(void);

/**
 * 执行配置获取任务
 *
 * @param ctx 上下文 (未使用)
 */
void config_fetch_task(void *ctx);

/**
 * 停止配置服务
 */
void config_service_stop(void);

#endif // WISE_DEPOT_CONFIG_SERVICE_H
