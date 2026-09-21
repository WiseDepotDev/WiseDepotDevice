/**
 * 系统监控模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_SYS_MONITOR_H
#define WISE_DEPOT_SYS_MONITOR_H

/**
 * 获取当前 CPU 使用率 (0.0 - 100.0)
 * 注意：首次调用可能返回 0.0
 *
 * @return CPU 使用率百分比
 */
/**
 * @brief 取 CPU 使用率百分比
 * @return CPU 使用率百分比（0-100）
 */
double sys_monitor_get_cpu_usage(void);

/**
 * 获取当前内存使用率 (0.0 - 100.0)
 *
 * @return 内存使用率百分比
 */
/**
 * @brief 取内存使用率百分比
 * @return 内存使用率百分比（0-100）
 */
double sys_monitor_get_mem_usage(void);

#endif // WISE_DEPOT_SYS_MONITOR_H
