/**
 * @file test_mem_safety.c
 * @brief 内存安全回归用例（P4-01：内存越界止血）。
 *
 * 覆盖三类此前的越界风险：
 * 1. bytes_to_hex 无容量参数（ASan 曾复现 stack-buffer-overflow：include/common/utils.h）；
 * 2. 累加式 snprintf 偏移写法（截断后 offset 越界 + size_t 下溢）；
 * 3. patrol_task_to_json 未校验 action_count 就去索引固定长度数组。
 */

#include "unity.h"
#include "common/utils.h"
#include "domain/patrol_task.h"
#include <stdlib.h>
#include <string.h>

/** 统计子串出现次数（用于断言动作条目数量） */
static int count_substring(const char *haystack, const char *needle) {
    int count = 0;
    const char *p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

/** 容量足够时应完整转换；容量不足时按整字节截断且绝不越界 */
void test_bytes_to_hex_respects_capacity(void) {
    uint8_t bytes[32];
    memset(bytes, 0xAB, sizeof(bytes));

    char full[65];
    memset(full, 0x7F, sizeof(full));
    size_t written = bytes_to_hex(bytes, sizeof(bytes), full, sizeof(full));
    TEST_ASSERT_EQUAL(64, (int)written);
    TEST_ASSERT_EQUAL(64, (int)strlen(full));
    TEST_ASSERT_EQUAL(0, strncmp(full, "ABABABABABABABAB", 16)); /* 前 16 字符即前 8 字节 */
    TEST_ASSERT_EQUAL('\0', full[64]);

    /* 容量 9 → 只能容纳 4 个字节（8 字符 + NUL），其余截断 */
    char small[9];
    memset(small, 0x7F, sizeof(small));
    written = bytes_to_hex(bytes, sizeof(bytes), small, sizeof(small));
    TEST_ASSERT_EQUAL(8, (int)written);
    TEST_ASSERT_EQUAL(8, (int)strlen(small));
    TEST_ASSERT_EQUAL_STRING("ABABABAB", small);
    TEST_ASSERT_EQUAL('\0', small[8]);

    /* 边界入参 */
    char guard[4] = {'x', 'x', 'x', '\0'};
    TEST_ASSERT_EQUAL(0, (int)bytes_to_hex(NULL, 8, guard, sizeof(guard)));
    TEST_ASSERT_EQUAL_STRING("", guard);
    TEST_ASSERT_EQUAL(0, (int)bytes_to_hex(bytes, sizeof(bytes), guard, 0));
}

/** 安全累加：无论怎么截断，都不会写出缓冲区且始终 NUL 结尾 */
void test_wd_str_appendf_never_overflows(void) {
    char buf[16];
    memset(buf, 0x7F, sizeof(buf));
    size_t used = 0;

    for (int i = 0; i < 5; i++) {
        used = wd_str_appendf(buf, sizeof(buf), used, "abcdefghij");
    }

    TEST_ASSERT_TRUE(used <= sizeof(buf) - 1);
    /* 越界写会破坏哨兵，这里用 strlen 必须能安全读到结尾来间接验证 */
    TEST_ASSERT_EQUAL((int)used, (int)strlen(buf));
    TEST_ASSERT_EQUAL('\0', buf[sizeof(buf) - 1]);
}

/** action_count 超过数组上限时应被夹取，不越界读 actions[] */
void test_patrol_task_to_json_clamps_action_count(void) {
    patrol_task_t *task = patrol_task_create("task-1", "clamp-check");
    TEST_ASSERT_NOT_NULL(task);

    for (uint8_t i = 0; i < PATROL_TASK_MAX_ACTIONS; i++) {
        patrol_action_t action;
        memset(&action, 0, sizeof(action));
        action.type = PATROL_ACTION_MOVE_FORWARD;
        action.speed = 50;
        action.duration_ms = 1000;
        TEST_ASSERT_EQUAL(0, patrol_task_add_action(task, &action));
    }

    /* 人为把计数改成越界值：旧实现会顺着 actions[] 之后的内存继续读 */
    task->action_count = PATROL_TASK_MAX_ACTIONS + 50;

    char *json = patrol_task_to_json(task);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(strlen(json) < 4096);
    TEST_ASSERT_EQUAL(PATROL_TASK_MAX_ACTIONS, count_substring(json, "\"duration\""));
    TEST_ASSERT_EQUAL('}', json[strlen(json) - 1]);

    free(json);
    patrol_task_free(task);
}
