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
/**
 * @brief 初始化设备服务（调度器、配置、日志、巡检、RFID）
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t device_service_init(void);

/**
 * 执行设备注册流程
 *
 * @return 0 成功，-1 失败
 */
/**
 * @brief 向服务端注册设备并保存返回的令牌
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t device_register(void);

/**
 * 执行心跳上报流程
 *
 * @return 0 成功，-1 失败
 */
/**
 * @brief 发送一次心跳
 * @return 0 成功；负值失败
 */
int device_heartbeat(void);

/**
 * 启动设备主循环 (阻塞)
 */
/**
 * @brief 启动设备服务主循环（阻塞）
 */
void device_run(void);

/**
 * 停止设备服务
 */
/**
 * @brief 请求停止设备服务
 */
void device_stop(void);

/**
 * 触发重新认证 (由心跳任务调用)
 */
/**
 * @brief 标记需要重新注册（令牌失效时调用）
 */
void device_trigger_reauth(void);

/**
 * MQTT 消息回调
 * 
 * @param topic 主题
 * @param payload 消息内容
 */
/**
 * @brief 处理下行的 MQTT 消息（任务下发 / 配置更新）
 * @param topic MQTT 主题
 * @param payload 报文内容
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
/**
 * @brief 设置当前访问令牌与刷新令牌
 * @param access_token 参数
 * @param refresh_token 刷新令牌
 */
void device_service_set_tokens(const char *access_token, const char *refresh_token);

/**
 * 刷新访问令牌
 *
 * @return 0 成功，-1 失败
 */
/**
 * @brief 用刷新令牌换取新的访问令牌
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t device_refresh_token(void);

#endif // WISE_DEPOT_DEVICE_SERVICE_H
