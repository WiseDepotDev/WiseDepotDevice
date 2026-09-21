#include "common/wd_error.h"
/**
 * 设备服务模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_DEVICE_SERVICE_H
#define WISE_DEPOT_DEVICE_SERVICE_H

/**
 * 初始化设备服务
 *
 * @return 0 成功，-1 失败
 */
wd_error_t device_service_init(void);

/**
 * 执行设备注册流程
 *
 * @return 0 成功，-1 失败
 */
wd_error_t device_register(void);

/**
 * 执行心跳上报流程
 *
 * @return 0 成功，-1 失败
 */
int device_heartbeat(void);

/**
 * 启动设备主循环 (阻塞)
 */
void device_run(void);

/**
 * 停止设备服务
 */
void device_stop(void);

/**
 * 触发重新认证 (由心跳任务调用)
 */
void device_trigger_reauth(void);

/**
 * MQTT 消息回调
 * 
 * @param topic 主题
 * @param payload 消息内容
 */
void device_on_mqtt_message(const char *topic, const char *payload);

/**
 * 获取当前访问令牌
 *
 * @return 访问令牌字符串，如果没有则返回NULL
 */
char *device_service_get_token(void);

/**
 * 获取当前刷新令牌
 *
 * @return 刷新令牌字符串，如果没有则返回NULL
 */
char *device_service_get_refresh_token(void);

/**
 * 设置访问令牌和刷新令牌
 *
 * @param access_token 访问令牌
 * @param refresh_token 刷新令牌
 */
void device_service_set_tokens(const char *access_token, const char *refresh_token);

/**
 * 刷新访问令牌
 *
 * @return 0 成功，-1 失败
 */
wd_error_t device_refresh_token(void);

#endif // WISE_DEPOT_DEVICE_SERVICE_H
