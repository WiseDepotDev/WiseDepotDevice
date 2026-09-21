#include "common/wd_error.h"
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
/**
 * @brief 初始化配置服务
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t config_service_init(void);

/**
 * 从服务端拉取配置并合并（启动阶段的一次性拉取；P4-08 从 common 层迁入）
 *
 * 需要 server_url 与 device_id 已就绪（即 config_load() 之后调用）；失败只记录告警，
 * 不改变已有配置、也不影响启动。**这是无签名的最佳努力路径**；带签名与 Token 的
 * 定时拉取见 config_fetch_task()。
 *
 * @return 0 成功拉取并合并，-1 跳过或失败
 */
/**
 * @brief 启动阶段的一次性远端配置拉取（无签名，失败不影响启动）
 * @return 0 成功；负值失败
 */
int config_service_fetch_remote(void);

/**
 * 执行配置获取任务
 *
 * @param ctx 上下文 (未使用)
 */
/**
 * @brief 定时拉取远端配置（带签名与令牌）
 * @param ctx 参数
 */
void config_fetch_task(void *ctx);

/**
 * 停止配置服务
 */
/**
 * @brief 停止配置服务
 */
void config_service_stop(void);

#endif // WISE_DEPOT_CONFIG_SERVICE_H
