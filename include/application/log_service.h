#include "common/wd_error.h"
/**
 * 日志上传服务模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-13
 */

#ifndef WISE_DEPOT_LOG_SERVICE_H
#define WISE_DEPOT_LOG_SERVICE_H

/**
 * 初始化日志上传服务
 *
 * @return 0 成功，-1 失败
 */
/**
 * @brief 初始化日志服务
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t log_service_init(void);

/**
 * 执行日志上传任务
 *
 * @param ctx 上下文 (未使用)
 */
/**
 * @brief 扫描并上传待上传日志文件
 * @param ctx 参数
 */
void log_upload_task(void *ctx);

/**
 * 停止日志上传服务
 */
/**
 * @brief 停止日志服务
 */
void log_service_stop(void);

#endif // WISE_DEPOT_LOG_SERVICE_H
