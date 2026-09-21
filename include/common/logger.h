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
/**
 * @brief 初始化日志（可选文件输出），可重复调用
 * @param log_file 日志文件路径
 * @param level 电平/布尔值
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t logger_init(const char *log_file, log_level_t level);

/**
 * 重新打开日志文件 (用于日志轮转)
 *
 * @param new_log_file 新日志文件路径 (可选，NULL 则使用原路径)
 * @return 0 成功，-1 失败
 */
/**
 * @brief 切换日志文件路径（不丢已写入内容）
 * @param new_log_file 新的日志文件路径
 * @return 0 成功；负值失败
 */
int logger_reopen(const char *new_log_file);

/**
 * 关闭日志系统
 */
/**
 * @brief 关闭日志文件并释放日志资源
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
/**
 * @brief 按级别写一条日志（线程安全）
 * @param level 电平/布尔值
 * @param file 参数
 * @param line 参数
 * @param fmt 参数
 */
void logger_log(log_level_t level, const char *file, int line, const char *fmt, ...);

/* 便捷宏定义 */
#define LOG_DEBUG(...) logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  logger_log(LOG_LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif // WISE_DEPOT_LOGGER_H
