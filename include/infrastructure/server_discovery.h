/**
 * 服务端发现模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-13
 */

#ifndef WISE_DEPOT_SERVER_DISCOVERY_H
#define WISE_DEPOT_SERVER_DISCOVERY_H

#include "common/wd_error.h"
#include <stdbool.h>

/**
 * 初始化服务端发现模块
 *
 * @return 0 成功，-1 失败
 */
/**
 * @brief 初始化 UDP 广播发现（建 socket、设超时）
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t server_discovery_init(void);

/**
 * 启动服务发现广播 (阻塞直到发现服务端或超时)
 *
 * @param timeout_sec 超时时间 (秒)，0 表示无限等待
 * @return 0 成功发现并更新配置，-1 失败/超时
 */
/**
 * @brief 广播发现服务端并写回配置（失败时用兜底地址）
 * @param timeout_sec 超时（秒）
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t server_discovery_start(int timeout_sec);

/**
 * 停止服务发现
 */
/**
 * @brief 关闭发现用 socket
 */
void server_discovery_stop(void);

#endif // WISE_DEPOT_SERVER_DISCOVERY_H
