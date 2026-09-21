/**
 * 日志系统模块头文件
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#ifndef WISE_DEPOT_LOGGER_H
#define WISE_DEPOT_LOGGER_H

#include "common/wd_error.h"
#include <stdarg.h>

/* 日志级别定义 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_NONE  = 4
} log_level_t;

/**
 * 初始化日志系统
 *
 * @param log_file 日志文件路径 (NULL 则仅输出到控制台)
 * @param level 最低日志级别
 * @return 0 成功，-1 失败
 */
wd_error_t logger_init(const char *log_file, log_level_t level);

/**
 * 重新打开日志文件 (用于日志轮转)
 *
 * @param new_log_file 新日志文件路径 (可选，NULL 则使用原路径)
 * @return 0 成功，-1 失败
 */
int logger_reopen(const char *new_log_file);

/**
 * 关闭日志系统
 */
void logger_close(void);

/**
 * 记录日志 (底层函数，建议使用宏)
 *
 * @param level 日志级别
 * @param file 源文件名
 * @param line 行号
 * @param fmt 格式化字符串
 * @param ... 参数
 */
void logger_log(log_level_t level, const char *file, int line, const char *fmt, ...);

/* 便捷宏定义 */
#define LOG_DEBUG(...) logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif // WISE_DEPOT_LOGGER_H
