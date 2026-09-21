#include "domain/inventory_manager.h"
#include "common/envelope.h"
#include "common/logger.h"
#include "common/utils.h"
#include "common/xmalloc.h"
#include "common/wd_error.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CACHE_FILE "inventory_cache.json"
#define MAX_EXPECTED_PRODUCTS 1000

// Internal structure for expected products
typedef struct {
    long productId;
    char productName[128];
    char productCode[65];
    int quantity;
    int scanned_count;
} expected_product_t;

static expected_product_t *expected_products = NULL;
static size_t product_count = 0;

void inventory_mgr_init(void) {
    /* P4-01：先释放上一份，重复 init 不再泄漏（此前 1000 条约 216 KB 每次都泄漏） */
    inventory_mgr_free();
    expected_products = (expected_product_t *)xmalloc_try(sizeof(expected_product_t) * MAX_EXPECTED_PRODUCTS);
    if (!expected_products) {
        /* P4-06：内存不足不退出进程；后续加载/比对会各自返回错误码 */
        LOG_ERROR("分配预期库存表失败（内存不足），盘点功能不可用");
        product_count = 0;
        return;
    }
    product_count = 0;
}

void inventory_mgr_free(void) {
    if (expected_products) {
        xfree(expected_products);
        expected_products = NULL;
    }
    product_count = 0;
}

int inventory_load_expected(const char *json_str) {
    wd_envelope_t envelope;
    cJSON *rows = NULL;
    cJSON *item = NULL;
    int rc;

    if (json_str == NULL || expected_products == NULL) {
        LOG_WARN("Expected inventory: manager not initialized or out of memory");
        /* P4-11：区分「预期库存表为空（OOM/未初始化）」与「入参为空」，不再共用一个魔数 */
        return (expected_products == NULL) ? WD_ERR_NOMEM : WD_ERR_PARAM;
    }

    /* 统一信封解析：唯一解析入口，不再兼容「根节点直接带 code」等历史格式 */
    rc = envelope_parse(json_str, &envelope);
    if (rc != WD_ENVELOPE_OK) {
        LOG_WARN("Expected inventory: envelope parse failed, rc=%d", rc);
        return WD_ERR_PARAM;
    }
    if (!envelope_is_success(&envelope)) {
        LOG_WARN("Expected inventory: business failed, code=%s, errorCode=%s",
                 envelope_code(&envelope) != NULL ? envelope_code(&envelope) : "(null)",
                 envelope_error_code(&envelope) != NULL ? envelope_error_code(&envelope) : "(null)");
        envelope_free(&envelope);
        return WD_ERR_SERVER;
    }

    rows = envelope_data_rows(&envelope);
    if (!cJSON_IsArray(rows)) {
        LOG_WARN("Expected inventory: no list payload in data");
        envelope_free(&envelope);
        return WD_ERR_PARAM;
    }

    product_count = 0;
    cJSON_ArrayForEach(item, rows) {
        const char *product_code = envelope_str(item, "productCode");
        const char *product_name = envelope_str(item, "productName");

        if (product_count >= MAX_EXPECTED_PRODUCTS) {
            break;
        }
        if (product_code == NULL) {
            continue;
        }

        expected_products[product_count].productId = envelope_int(item, "productId", 0);
        (void)snprintf(expected_products[product_count].productName,
                       sizeof(expected_products[product_count].productName),
                       "%s", product_name != NULL ? product_name : "");
        (void)snprintf(expected_products[product_count].productCode,
                       sizeof(expected_products[product_count].productCode),
                       "%s", product_code);
        expected_products[product_count].quantity = (int)envelope_int(item, "quantity", 1);
        expected_products[product_count].scanned_count = 0;

        product_count++;
    }

    envelope_free(&envelope);
    LOG_INFO("Loaded %zu expected products", product_count);
    return 0;
}

inventory_report_t *inventory_process_scan(const rfid_tag_t *scanned_tags, size_t count) {
    inventory_report_t *report = (inventory_report_t *)xcalloc_try(1, sizeof(inventory_report_t));
    if (!report) {
        LOG_ERROR("分配盘点报告失败（内存不足）");
        return NULL;
    }
    
    report->total_scanned = (int)count;
    report->total_expected = 0;
    for(size_t i=0; i<product_count; i++) {
        report->total_expected += expected_products[i].quantity;
        expected_products[i].scanned_count = 0; // Reset
    }
    
    // Allocate items (scanned only)
    report->items = (inventory_item_t *)xcalloc_try(count > 0 ? count : 1, sizeof(inventory_item_t));
    if (!report->items) {
        LOG_ERROR("分配盘点明细失败（内存不足）");
        xfree(report);
        return NULL;
    }
    report->item_count = 0;
    
    // Process Scanned Tags
    for (size_t i = 0; i < count; i++) {
        char epc_str[65];
        char tid_str[65];
        bytes_to_hex(scanned_tags[i].epc, scanned_tags[i].epc_len, epc_str, sizeof(epc_str));
        bytes_to_hex(scanned_tags[i].tid, scanned_tags[i].tid_len, tid_str, sizeof(tid_str));
        
        bool found = false;
        for (size_t j = 0; j < product_count; j++) {
            if (strcmp(epc_str, expected_products[j].productCode) == 0) {
                expected_products[j].scanned_count++;
                found = true;
                break;
            }
        }
        
        // Add to details
        inventory_item_t *item = &report->items[report->item_count++];
        snprintf(item->epc, sizeof(item->epc), "%s", epc_str);
        snprintf(item->tid, sizeof(item->tid), "%s", tid_str);
        item->timestamp = time(NULL);
        item->status = found ? TAG_STATUS_NORMAL : TAG_STATUS_SURPLUS;
        
        if (found) report->match_count++;
        else report->surplus_count++;
    }
    
    // Calculate Differences
    // 预留空间：预期产品差异 + 未录入系统的RFID
    size_t max_diffs = product_count + count;
    report->differences = (inventory_difference_t *)xcalloc_try(max_diffs > 0 ? max_diffs : 1, sizeof(inventory_difference_t));
    if (!report->differences) {
        LOG_ERROR("分配盘点差异表失败（内存不足）");
        xfree(report->items);
        xfree(report);
        return NULL;
    }
    report->diff_count = 0;
    
    // 1. 处理预期产品的差异
    for (size_t i = 0; i < product_count; i++) {
        expected_product_t *p = &expected_products[i];
        int diff = p->scanned_count - p->quantity;
        
        if (diff != 0) {
            inventory_difference_t *d = &report->differences[report->diff_count++];
            d->productId = p->productId;
            snprintf(d->productName, sizeof(d->productName), "%s", p->productName);
            snprintf(d->productCode, sizeof(d->productCode), "%s", p->productCode);
            d->expectedQuantity = p->quantity;
            d->scannedQuantity = p->scanned_count;
            d->difference = diff;
            
            if (diff < 0) {
                strcpy(d->status, "MISSING");
                report->loss_count += -diff;
            } else {
                strcpy(d->status, "EXTRA");
            }
        }
    }
    
    // 2. 处理未录入系统的RFID
    for (size_t i = 0; i < count; i++) {
        char epc_str[65];
        bytes_to_hex(scanned_tags[i].epc, scanned_tags[i].epc_len, epc_str, sizeof(epc_str));
        
        bool found = false;
        for (size_t j = 0; j < product_count; j++) {
            if (strcmp(epc_str, expected_products[j].productCode) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            inventory_difference_t *d = &report->differences[report->diff_count++];
            d->productId = 0;
            d->productName[0] = '\0';
            snprintf(d->productCode, sizeof(d->productCode), "%s", epc_str);
            d->expectedQuantity = 0;
            d->scannedQuantity = 1;
            d->difference = 1;
            strcpy(d->status, "UNKNOWN");
        }
    }
    
    return report;
}

void inventory_free_report(inventory_report_t *report) {
    if (report) {
        if (report->items) xfree(report->items);
        if (report->differences) xfree(report->differences);
        xfree(report);
    }
}

char *inventory_report_to_json(const inventory_report_t *report, const char *task_id) {
    cJSON *root = cJSON_CreateObject();
    if (task_id) {
        cJSON_AddStringToObject(root, "taskId", task_id);
    }
    cJSON_AddNumberToObject(root, "total_scanned", report->total_scanned);
    cJSON_AddNumberToObject(root, "total_expected", report->total_expected);
    cJSON_AddNumberToObject(root, "match_count", report->match_count);
    cJSON_AddNumberToObject(root, "surplus_count", report->surplus_count);
    cJSON_AddNumberToObject(root, "loss_count", report->loss_count);
    cJSON_AddNumberToObject(root, "abnormal_count", report->abnormal_count);
    
    cJSON *items = cJSON_CreateArray();
    for (size_t i = 0; i < report->item_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "epc", report->items[i].epc);
        cJSON_AddStringToObject(item, "tid", report->items[i].tid);
        
        /* 初值 "unknown" 是**故意的**防御：switch 目前覆盖全部枚举值，但新增枚举时
         * 不应输出未初始化的字符串（clang-analyzer-deadcode.DeadStores 误报）。 */
        const char *status_str = "unknown"; // NOLINT(clang-analyzer-deadcode.DeadStores)
        switch (report->items[i].status) {
            case TAG_STATUS_NORMAL: status_str = "normal"; break;
            case TAG_STATUS_SURPLUS: status_str = "surplus"; break;
            case TAG_STATUS_LOSS: status_str = "loss"; break;
            case TAG_STATUS_ABNORMAL: status_str = "abnormal"; break;
        }
        cJSON_AddStringToObject(item, "status", status_str);
        cJSON_AddNumberToObject(item, "timestamp", (double)report->items[i].timestamp);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "details", items);
    
    // differences
    cJSON *diffs = cJSON_CreateArray();
    for (size_t i = 0; i < report->diff_count; i++) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddNumberToObject(d, "productId", (double)report->differences[i].productId);
        cJSON_AddStringToObject(d, "productName", report->differences[i].productName);
        cJSON_AddStringToObject(d, "productCode", report->differences[i].productCode);
        cJSON_AddNumberToObject(d, "expectedQuantity", report->differences[i].expectedQuantity);
        cJSON_AddNumberToObject(d, "scannedQuantity", report->differences[i].scannedQuantity);
        cJSON_AddNumberToObject(d, "difference", report->differences[i].difference);
        cJSON_AddStringToObject(d, "status", report->differences[i].status);
        cJSON_AddItemToArray(diffs, d);
    }
    cJSON_AddItemToObject(root, "differences", diffs);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

wd_error_t inventory_cache_save(const inventory_report_t *report) {
    char *json = inventory_report_to_json(report, NULL); /* 离线缓存不绑定任务号 */    if (!json) return WD_ERR_NOMEM;
    
    FILE *f = fopen(CACHE_FILE, "a"); // Append mode
    if (!f) {
        xfree(json);
        return WD_ERR_IO;
    }
    
    fprintf(f, "%s\n", json);
    fclose(f);
    xfree(json);
    return 0;
}

char *inventory_cache_load(void) {
    /* 读取整个缓存文件（调用方负责上传成功后清理）；
     * 生产化需要「读一批 → 上传成功 → 删除该批」的分段清理。 */
    FILE *f = fopen(CACHE_FILE, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0) {
        fclose(f);
        return NULL;
    }
    
    char *content = (char *)xmalloc_try(fsize + 1);
    if (!content) {
        LOG_ERROR("分配缓存内容缓冲失败（内存不足）");
        fclose(f);
        return NULL;
    }
    fread(content, 1, fsize, f);
    content[fsize] = '\0';
    
    fclose(f);
    return content;
}

void inventory_cache_clear(void) {
    unlink(CACHE_FILE);
}
