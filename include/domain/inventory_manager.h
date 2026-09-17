#ifndef INVENTORY_MANAGER_H
#define INVENTORY_MANAGER_H

#include "infrastructure/rfid_driver.h"
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
void inventory_mgr_init(void);

// Load expected inventory from JSON string (from server)
int inventory_load_expected(const char *json_str);

// Process scan results against expected inventory
// Returns a report that must be freed with inventory_free_report
inventory_report_t *inventory_process_scan(const rfid_tag_t *scanned_tags, size_t count);

// Free report
void inventory_free_report(inventory_report_t *report);

// Generate JSON string from report
char *inventory_report_to_json(const inventory_report_t *report, const char *task_id);

// Offline Cache Operations
int inventory_cache_save(const inventory_report_t *report);
char *inventory_cache_load(void);
void inventory_cache_clear(void);

#endif // INVENTORY_MANAGER_H
