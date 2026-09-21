#ifndef WISE_DEPOT_INVENTORY_MANAGER_H
#define WISE_DEPOT_INVENTORY_MANAGER_H

#include "domain/tag.h"
#include "common/wd_error.h"
#include <cJSON.h>
#include <time.h>

typedef enum {
    TAG_STATUS_NORMAL,
    TAG_STATUS_SURPLUS, // 盘盈
    TAG_STATUS_LOSS,    // 盘亏
    TAG_STATUS_ABNORMAL // 异常 (EPC match but TID mismatch)
} tag_status_t;

typedef struct {
    long productId;
    char productName[128];
    char productCode[65];
    int expectedQuantity;
    int scannedQuantity;
    int difference;
    char status[16]; // MISSING, EXTRA, NORMAL
} inventory_difference_t;

typedef struct {
    char epc[65]; // Hex string
    char tid[65]; // Hex string
    tag_status_t status;
    time_t timestamp;
} inventory_item_t;

typedef struct {
    int total_scanned;
    int total_expected;
    int match_count;
    int surplus_count;
    int loss_count;
    int abnormal_count;
    inventory_item_t *items; // Array of scanned items
    size_t item_count;
    inventory_difference_t *differences; // Array of aggregated differences
    size_t diff_count;
} inventory_report_t;

// Initialize inventory manager
/**
 * @brief 初始化预期库存表
 */
void inventory_mgr_init(void);

// Release inventory manager resources (P4-01: 预期库存数组约 216 KB；init 可重复调用，会先释放上一份)
/**
 * @brief 释放预期库存表
 */
void inventory_mgr_free(void);

// Load expected inventory from JSON string (from server)
/**
 * @brief 从标准信封解析并加载预期库存明细
 * @param json_str 标准信封 JSON 文本
 * @return WD_OK 成功；其余为负的错误码
 */
int inventory_load_expected(const char *json_str);

// Process scan results against expected inventory
// Returns a report that must be freed with inventory_free_report
inventory_report_t *inventory_process_scan(const rfid_tag_t *scanned_tags, size_t count);

// Free report
/**
 * @brief 释放盘点报告内部资源
 * @param report 盘点报告
 */
void inventory_free_report(inventory_report_t *report);

// Generate JSON string from report
char *inventory_report_to_json(const inventory_report_t *report, const char *task_id);

// Offline Cache Operations
/**
 * @brief 把盘点报告追加写入离线缓存
 * @param report 盘点报告
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t inventory_cache_save(const inventory_report_t *report);
char *inventory_cache_load(void);
/**
 * @brief 清空离线缓存文件
 */
void inventory_cache_clear(void);

#endif // WISE_DEPOT_INVENTORY_MANAGER_H
