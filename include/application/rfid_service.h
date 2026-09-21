#ifndef WISE_DEPOT_RFID_SERVICE_H
#define WISE_DEPOT_RFID_SERVICE_H

#include <stdbool.h>
#include "domain/tag.h"
#include "domain/inventory_manager.h"
#include "common/wd_error.h"

typedef struct {
    char serial_port[64];
    int baudrate;
    int power_dbm;       /**< P4-12：初始化时下发的功率（0 = 不下发） */
    int address;         /**< P4-12：读头地址（0xFF = 广播） */
    bool legacy_frames;  /**< P4-12：true = 沿用旧帧路径 */
    const char *server_url; // Base URL for API
    const char *mqtt_topic;
} rfid_service_config_t;

/**
 * @brief 初始化 RFID 服务（驱动 + 盘点管理器）
 * @param config 配置
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t rfid_service_init(const rfid_service_config_t *config);
/**
 * @brief 释放 RFID 服务资源
 */
void rfid_service_cleanup(void);

// Trigger a full inventory cycle: Scan -> Fetch Expected -> Compare -> Report
// @param task_id Optional task ID to associate the report with a specific patrol task. Can be NULL.
/**
 * @brief 执行一次完整盘点：扫描 → 拉预期 → 比对 → 上报
 * @param task_id 任务号
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t rfid_service_run_cycle(const char *task_id);

// Check if service is busy
/**
 * @brief 取 RFID 服务是否正在盘点
 * @return true = 正在盘点
 */
bool rfid_service_is_busy(void);

/** 仅从服务端拉取预期库存并加载到内存，供巡检开始时调用一次 */
/**
 * @brief 仅拉取并加载预期库存
 * @return 0 成功；负值失败
 */
int rfid_service_fetch_expected_inventory(void);

/** 仅执行 RFID 盘点扫描，不拉取库存、不比对、不上传。供巡检过程中累积标签用。返回扫描到的标签数，失败返回 -1 */
/**
 * @brief 仅执行扫描（供巡检过程累积标签）
 * @param tags 标签数组
 * @param max_count 参数
 * @return 0 成功；负值失败
 */
int rfid_service_scan_only(rfid_tag_t *tags, int max_count);

/** 上传巡检报告到服务端（服务端会分发给手机展示差异列表） */
/**
 * @brief 上传盘点报告
 * @param report 盘点报告
 * @param task_id 任务号
 */
void rfid_service_upload_inspection_report(const inventory_report_t *report, const char *task_id);

#endif // WISE_DEPOT_RFID_SERVICE_H
