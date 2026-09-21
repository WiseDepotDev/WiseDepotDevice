#ifndef WISE_DEPOT_RFID_SERVICE_H
#define WISE_DEPOT_RFID_SERVICE_H

#include <stdbool.h>
#include "domain/tag.h"
#include "domain/inventory_manager.h"

typedef struct {
    char serial_port[64];
    int baudrate;
    const char *server_url; // Base URL for API
    const char *mqtt_topic;
} rfid_service_config_t;

int rfid_service_init(const rfid_service_config_t *config);
void rfid_service_cleanup(void);

// Trigger a full inventory cycle: Scan -> Fetch Expected -> Compare -> Report
// @param task_id Optional task ID to associate the report with a specific patrol task. Can be NULL.
int rfid_service_run_cycle(const char *task_id);

// Check if service is busy
bool rfid_service_is_busy(void);

/** 仅从服务端拉取预期库存并加载到内存，供巡检开始时调用一次 */
int rfid_service_fetch_expected_inventory(void);

/** 仅执行 RFID 盘点扫描，不拉取库存、不比对、不上传。供巡检过程中累积标签用。返回扫描到的标签数，失败返回 -1 */
int rfid_service_scan_only(rfid_tag_t *tags, int max_count);

/** 上传巡检报告到服务端（服务端会分发给手机展示差异列表） */
void rfid_service_upload_inspection_report(const inventory_report_t *report, const char *task_id);

#endif // WISE_DEPOT_RFID_SERVICE_H
