/**
 * @file test_rfid.c
 * @brief 盘点预期库存加载与结果比对单元测试。
 *
 * P4-02（测试基线对齐）：
 * - 输入必须是 STD-CONTRACT-01 的**标准信封**（`header` + `payload`），历史格式（根节点直接是数组）已不再兼容；
 * - 预期库存明细的字段名是 **productCode**（此前用例写成 `epc`，导致 envelope 解析后无有效行）；
 * - 实现以「扫码 EPC 的十六进制字符串 == 预期 productCode」判定匹配，**TID 只记录、不参与比对**，
 *   因此 `abnormal_count` 恒为 0（该缺口已登记，见任务清单 P4-12 RFID 协议对齐）。
 */

#include "unity.h"
#include "common/error_code.h"
#include "domain/inventory_manager.h"
#include "infrastructure/rfid_driver.h"
#include <stdlib.h>
#include <string.h>

/** 标准成功信封：data.rows 为预期库存明细（字段名 productCode，成功码取生成器宏） */
static const char *EXPECTED_ENVELOPE =
    "{"
    "\"header\":{\"request_id\":\"req-inv-1\",\"packet_type\":\"INVENTORY_EXPECTED_LIST\",\"timestamp\":1772000000123},"
    "\"payload\":{\"code\":\"" WD_SUCCESS "\",\"message\":\"ok\",\"data\":{\"total\":3,\"rows\":["
    "{\"productId\":1,\"productName\":\"甲\",\"productCode\":\"A1\",\"quantity\":1},"
    "{\"productId\":2,\"productName\":\"乙\",\"productCode\":\"A2\",\"quantity\":1},"
    "{\"productId\":3,\"productName\":\"丙\",\"productCode\":\"A3\",\"quantity\":1}]}}"
    "}";

/** 业务失败信封（非成功码）——必须被拒绝 */
static const char *FAILED_ENVELOPE =
    "{"
    "\"header\":{\"request_id\":\"req-inv-2\",\"packet_type\":\"INVENTORY_EXPECTED_LIST\",\"timestamp\":2},"
    "\"payload\":{\"code\":\"VAL-CONFLICT-PERMISSION-1001\",\"message\":\"denied\","
    "\"errorCode\":\"VAL-CONFLICT-PERMISSION-1001\",\"data\":null}"
    "}";

/** 预期库存加载：成功信封返回 0，NULL 与业务失败信封返回 -1 */
void test_inventory_load_expected(void) {
    inventory_mgr_init();

    TEST_ASSERT_EQUAL(0, inventory_load_expected(EXPECTED_ENVELOPE));
    TEST_ASSERT_EQUAL(-1, inventory_load_expected(NULL));
    TEST_ASSERT_EQUAL(-1, inventory_load_expected(FAILED_ENVELOPE));

    inventory_mgr_free();
}

/** 扫码结果比对：按 EPC↔productCode 匹配，统计匹配/盘盈/盘亏与差异明细 */
void test_inventory_process_scan(void) {
    inventory_mgr_init();
    TEST_ASSERT_EQUAL(0, inventory_load_expected(EXPECTED_ENVELOPE));

    rfid_tag_t scanned[3];
    memset(scanned, 0, sizeof(scanned));

    /* 已扫 A1（命中预期）、A2（命中预期）、A4（未录入 → 盘盈）；预期中的 A3 未扫 → 盘亏 */
    scanned[0].epc_len = 1;
    scanned[0].epc[0] = 0xA1;
    scanned[0].tid_len = 1;
    scanned[0].tid[0] = 0xB1;

    scanned[1].epc_len = 1;
    scanned[1].epc[0] = 0xA2;
    scanned[1].tid_len = 1;
    scanned[1].tid[0] = 0xB3; /* TID 与预期不一致：当前实现不比对，仅记录 */

    scanned[2].epc_len = 1;
    scanned[2].epc[0] = 0xA4;
    scanned[2].tid_len = 1;
    scanned[2].tid[0] = 0xB4;

    inventory_report_t *report = inventory_process_scan(scanned, 3);
    TEST_ASSERT_NOT_NULL(report);

    TEST_ASSERT_EQUAL(3, report->total_scanned);
    TEST_ASSERT_EQUAL(3, report->total_expected);
    TEST_ASSERT_EQUAL(2, report->match_count);    /* A1, A2（TID 不参与判定） */
    TEST_ASSERT_EQUAL(1, report->surplus_count);  /* A4 */
    TEST_ASSERT_EQUAL(1, report->loss_count);     /* A3 */
    TEST_ASSERT_EQUAL(0, report->abnormal_count); /* 当前实现不产生 abnormal */

    /* 明细项只含扫码结果，按扫码顺序 */
    TEST_ASSERT_EQUAL(3, (int)report->item_count);
    TEST_ASSERT_EQUAL_STRING("A1", report->items[0].epc);
    TEST_ASSERT_EQUAL_STRING("B1", report->items[0].tid);
    TEST_ASSERT_EQUAL(TAG_STATUS_NORMAL, report->items[0].status);
    TEST_ASSERT_EQUAL_STRING("A2", report->items[1].epc);
    TEST_ASSERT_EQUAL(TAG_STATUS_NORMAL, report->items[1].status);
    TEST_ASSERT_EQUAL_STRING("A4", report->items[2].epc);
    TEST_ASSERT_EQUAL(TAG_STATUS_SURPLUS, report->items[2].status);

    /* 差异明细：A3 盘亏（MISSING）+ A4 未录入（UNKNOWN） */
    TEST_ASSERT_EQUAL(2, (int)report->diff_count);

    bool found_missing = false;
    bool found_unknown = false;
    for (size_t i = 0; i < report->diff_count; i++) {
        const inventory_difference_t *d = &report->differences[i];
        if (strcmp(d->productCode, "A3") == 0) {
            TEST_ASSERT_EQUAL_STRING("MISSING", d->status);
            TEST_ASSERT_EQUAL(1, d->expectedQuantity);
            TEST_ASSERT_EQUAL(0, d->scannedQuantity);
            TEST_ASSERT_EQUAL(-1, d->difference);
            found_missing = true;
        } else if (strcmp(d->productCode, "A4") == 0) {
            TEST_ASSERT_EQUAL_STRING("UNKNOWN", d->status);
            TEST_ASSERT_EQUAL(0, d->expectedQuantity);
            TEST_ASSERT_EQUAL(1, d->scannedQuantity);
            TEST_ASSERT_EQUAL(1, d->difference);
            found_unknown = true;
        }
    }
    TEST_ASSERT_TRUE(found_missing);
    TEST_ASSERT_TRUE(found_unknown);

    inventory_free_report(report);
    inventory_mgr_free();
}
