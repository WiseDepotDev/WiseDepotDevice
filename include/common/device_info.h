/**
 * 设备信息模块头文件
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_DEVICE_INFO_H
#define WISE_DEPOT_DEVICE_INFO_H

/**
 * 设备元数据结构体
 */
typedef struct {
    char *os_name;      /**< 操作系统名称 (如 Ubuntu 22.04) */
    char *kernel_ver;   /**< 内核版本 (如 5.15.0-101-generic) */
    char *model;        /**< 设备型号 (如 WiseDevice-V1) */
    char *serial_no;    /**< 序列号 (可选，默认使用 device_id) */
} DeviceInfo;

/**
 * 获取当前设备信息
 * 首次调用时会执行系统探测，后续调用返回缓存
 *
 * @return 设备信息结构体指针 (由模块内部管理内存，不可释放)
 */
const DeviceInfo *device_info_get(void);

/**
 * 释放设备信息资源
 * 在程序退出前调用
 */
void device_info_free(void);

#endif // WISE_DEPOT_DEVICE_INFO_H
