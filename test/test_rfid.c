#include "unity.h"
#include "domain/inventory_manager.h"
#include "infrastructure/rfid_driver.h"
#include <string.h>
#include <stdlib.h>

void test_inventory_load_expected(void) {
    inventory_mgr_init();
    const char *json = "[{\"epc\":\"112233\",\"tid\":\"AABBCC\"}, {\"epc\":\"445566\"}]";
    int ret = inventory_load_expected(json);
    TEST_ASSERT_EQUAL(0, ret);
}

void test_inventory_process_scan(void) {
    inventory_mgr_init();
    // 1. Setup Expected
    const char *expected_json = "["
        "{\"epc\":\"E001\",\"tid\":\"T001\"}," // Normal match
        "{\"epc\":\"E002\",\"tid\":\"T002\"}," // Abnormal (TID mismatch)
        "{\"epc\":\"E003\"}"                   // Loss (Not scanned)
    "]";
    inventory_load_expected(expected_json);
    
    // 2. Setup Scanned
    rfid_tag_t scanned[3];
    memset(scanned, 0, sizeof(scanned));
    
    // Tag 1: Match E001/T001
    scanned[0].epc_len = 2;
    scanned[0].epc[0] = 0xE0; scanned[0].epc[1] = 0x01;
    scanned[0].tid_len = 2;
    scanned[0].tid[0] = 0xC0; scanned[0].tid[1] = 0x01; // wait, hex string "T001" vs bytes?
    // My implementation uses bytes_to_hex.
    // "E001" -> 0xE0 0x01
    // "T001" -> 0x54 0x30 0x30 0x31 ? No, usually hex string represents bytes.
    // "E001" -> 0xE0 0x01.
    
    // Let's use simple hex strings that map easily.
    // "A1" -> 0xA1.
    
    // Redo Expected
    const char *exp_json = "["
        "{\"epc\":\"A1\",\"tid\":\"B1\"}," // Normal
        "{\"epc\":\"A2\",\"tid\":\"B2\"}," // Abnormal (Scanned tid B3)
        "{\"epc\":\"A3\",\"tid\":\"B3\"}"  // Loss
    "]";
    inventory_load_expected(exp_json);
    
    // Tag 1: Match A1/B1
    scanned[0].epc_len = 1; scanned[0].epc[0] = 0xA1;
    scanned[0].tid_len = 1; scanned[0].tid[0] = 0xB1;
    
    // Tag 2: Abnormal A2/B3
    scanned[1].epc_len = 1; scanned[1].epc[0] = 0xA2;
    scanned[1].tid_len = 1; scanned[1].tid[0] = 0xB3;
    
    // Tag 3: Surplus A4
    scanned[2].epc_len = 1; scanned[2].epc[0] = 0xA4;
    scanned[2].tid_len = 1; scanned[2].tid[0] = 0xB4;
    
    // Process
    inventory_report_t *report = inventory_process_scan(scanned, 3);
    
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_EQUAL(3, report->total_scanned);
    TEST_ASSERT_EQUAL(3, report->total_expected);
    
    TEST_ASSERT_EQUAL(1, report->match_count);    // A1
    TEST_ASSERT_EQUAL(1, report->abnormal_count); // A2
    TEST_ASSERT_EQUAL(1, report->loss_count);     // A3
    TEST_ASSERT_EQUAL(1, report->surplus_count);  // A4
    
    // Check Items Details
    // A2 should be abnormal
    // A3 should be loss
    // A4 should be surplus
    // Order depends on implementation (scanned then expected).
    // Scanned: A1 (Match, skipped?), A2 (Abnormal), A4 (Surplus).
    // Expected: A3 (Loss).
    
    bool found_abnormal = false;
    bool found_surplus = false;
    bool found_loss = false;
    
    for (size_t i = 0; i < report->item_count; i++) {
        if (strcmp(report->items[i].epc, "A2") == 0) {
            TEST_ASSERT_EQUAL(TAG_STATUS_ABNORMAL, report->items[i].status);
            found_abnormal = true;
        }
        if (strcmp(report->items[i].epc, "A4") == 0) {
            TEST_ASSERT_EQUAL(TAG_STATUS_SURPLUS, report->items[i].status);
            found_surplus = true;
        }
        if (strcmp(report->items[i].epc, "A3") == 0) {
            TEST_ASSERT_EQUAL(TAG_STATUS_LOSS, report->items[i].status);
            found_loss = true;
        }
    }
    
    TEST_ASSERT_TRUE(found_abnormal);
    TEST_ASSERT_TRUE(found_surplus);
    TEST_ASSERT_TRUE(found_loss);
    
    inventory_free_report(report);
}
