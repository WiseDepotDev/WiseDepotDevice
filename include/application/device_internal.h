/**
 * @file device_internal.h
 * @brief 设备服务内部共享定义（P4-10 批次2b：随 device_service.c 拆分而引入）。
 *
 * 拆分后的分工：
 * - device_service.c：服务生命周期（init/run/stop/维护/信号）
 * - device_auth.c   ：注册与令牌（含本机 IP、JSON 取值助手）
 * - device_mqtt.c   ：MQTT 下发消息处理（任务下发 / 配置更新）
 *
 * 这是**模块内部**头文件，外部只应包含 application/device_service.h。
 */

#ifndef WISE_DEPOT_DEVICE_INTERNAL_H
#define WISE_DEPOT_DEVICE_INTERNAL_H

#include <stddef.h>

/** 版本号（原先只写在 device_service.c 里） */
#define DEVICE_VERSION "v1.0.0"

/* ===== 跨文件共享状态（唯一定义在 device_service.c） ===== */
extern volatile int g_running;        /**< 运行标志 */
extern volatile int g_reauth_needed;  /**< 需要重新认证 */
extern char *g_token;                 /**< 访问令牌 */
extern char *g_refresh_token;         /**< 刷新令牌 */

#endif // WISE_DEPOT_DEVICE_INTERNAL_H