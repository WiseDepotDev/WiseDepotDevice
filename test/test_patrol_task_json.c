/**
 * @file test_patrol_task_json.c
 * @brief P4-16：巡检任务 JSON 解析与服务端契约对齐的用例。
 *
 * 背景（P4-15 四步联调发现）：旧解析用手写"取引号字符串"的助手，而服务端
 * `TaskMessage` 里的 `taskId`/`taskType` 是**数字**、`actions` 是**数组**，于是
 * id 退化成 "unknown"、默认动作加不进去、actions 永远取不到。本文件把这四条契约钉死。
 */

#include "domain/patrol_task.h"
#include "common/xmalloc.h"
#include "application/patrol_service.h"
#include "unity.h"
#include <string.h>

/** 服务端 TaskMessage 的真实形状（无 actions，用 targetDistance 表达目标距离） */
static const char *SERVER_TASK_MESSAGE =
    "{\"taskId\":123,\"taskType\":1,\"targetDistance\":100.0,\"planId\":7,\"warehouseId\":3}";

/** 带 actions 数组的报文（与 patrol_task_to_json 的输出对称） */
static const char *TASK_WITH_ACTIONS =
    "{\"taskId\":\"9001\",\"name\":\"数组任务\",\"actions\":["
    "{\"type\":\"MOVE_FORWARD\",\"speed\":30,\"duration\":1500},"
    "{\"type\":\"SCAN\",\"speed\":0,\"duration_ms\":2000}]}";

void test_patrol_task_from_server_message(void) {
    patrol_task_t *task = patrol_task_from_json(SERVER_TASK_MESSAGE);

    TEST_ASSERT_NOT_NULL(task);
    /* 数字 taskId 必须变成可上报的十进制字符串（服务端按 Long 解析） */
    TEST_ASSERT_EQUAL_STRING("123", task->id);
    TEST_ASSERT_TRUE(patrol_task_id_is_numeric(task->id));
    TEST_ASSERT_EQUAL(PATROL_TASK_TYPE_MANUAL, task->type);
    /* 没有 actions 时按 targetDistance 折算一个前进动作：100cm / 20cm/s = 5s */
    TEST_ASSERT_EQUAL(1, task->action_count);
    TEST_ASSERT_EQUAL(PATROL_ACTION_MOVE_FORWARD, task->actions[0].type);
    TEST_ASSERT_EQUAL(5000, task->actions[0].duration_ms);

    patrol_task_free(task);
}

void test_patrol_task_from_actions_array(void) {
    patrol_task_t *task = patrol_task_from_json(TASK_WITH_ACTIONS);

    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_EQUAL_STRING("9001", task->id);
    TEST_ASSERT_EQUAL_STRING("数组任务", task->name);
    TEST_ASSERT_EQUAL(2, task->action_count);
    TEST_ASSERT_EQUAL(PATROL_ACTION_MOVE_FORWARD, task->actions[0].type);
    TEST_ASSERT_EQUAL(30, task->actions[0].speed);
    TEST_ASSERT_EQUAL(1500, task->actions[0].duration_ms);   /* duration 键名 */
    TEST_ASSERT_EQUAL(PATROL_ACTION_RFID_SCAN, task->actions[1].type); /* "SCAN" 别名 + 大小写不敏感（P4-16） */
    TEST_ASSERT_EQUAL(2000, task->actions[1].duration_ms);   /* duration_ms 键名 */

    patrol_task_free(task);
}

void test_patrol_task_from_json_rejects_bad_input(void) {
    TEST_ASSERT_TRUE(patrol_task_from_json(NULL) == NULL);
    TEST_ASSERT_TRUE(patrol_task_from_json("not a json") == NULL);
    TEST_ASSERT_TRUE(patrol_task_from_json("[1,2,3]") == NULL); /* 顶层必须是对象 */
}

void test_patrol_task_id_is_numeric(void) {
    TEST_ASSERT_TRUE(patrol_task_id_is_numeric("123"));
    TEST_ASSERT_TRUE(patrol_task_id_is_numeric("9001"));
    TEST_ASSERT_FALSE(patrol_task_id_is_numeric("12a"));
    TEST_ASSERT_FALSE(patrol_task_id_is_numeric("p415-smoke-1"));
    TEST_ASSERT_FALSE(patrol_task_id_is_numeric(""));
    TEST_ASSERT_FALSE(patrol_task_id_is_numeric(NULL));
}

void test_patrol_task_json_roundtrip(void) {
    patrol_task_t *task = patrol_task_from_json(TASK_WITH_ACTIONS);
    TEST_ASSERT_NOT_NULL(task);

    char *json = patrol_task_to_json(task);
    TEST_ASSERT_NOT_NULL(json);

    patrol_task_t *again = patrol_task_from_json(json);
    TEST_ASSERT_NOT_NULL(again);
    TEST_ASSERT_EQUAL_STRING(task->id, again->id);
    TEST_ASSERT_EQUAL(task->action_count, again->action_count);
    TEST_ASSERT_EQUAL(task->actions[0].type, again->actions[0].type);
    TEST_ASSERT_EQUAL(task->actions[0].speed, again->actions[0].speed);
    TEST_ASSERT_EQUAL(task->actions[1].duration_ms, again->actions[1].duration_ms);

    xfree(json);
    patrol_task_free(again);
    patrol_task_free(task);
}

void test_patrol_task_json_from_response(void) {
    /* 分页对象（服务端 GET /api/inspection/task 的真实形状） */
    const char *paged =
        "{\"code\":\"RES-0000\",\"data\":{\"total\":1,\"rows\":["
        "{\"taskId\":77,\"taskType\":1,\"targetDistance\":40.0}]}}";
    char *json = patrol_task_json_from_response(paged);
    TEST_ASSERT_NOT_NULL(json);
    patrol_task_t *task = patrol_task_from_json(json);
    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_EQUAL_STRING("77", task->id);
    TEST_ASSERT_EQUAL(2000, task->actions[0].duration_ms); /* 40cm / 20cm/s = 2s */
    xfree(json);
    patrol_task_free(task);

    /* 数组形状（兼容旧形状） */
    const char *arrayed =
        "{\"code\":\"RES-0000\",\"data\":[{\"taskId\":\"88\",\"actions\":[]}]}";
    json = patrol_task_json_from_response(arrayed);
    TEST_ASSERT_NOT_NULL(json);
    task = patrol_task_from_json(json);
    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_EQUAL_STRING("88", task->id);
    xfree(json);
    patrol_task_free(task);

    /* 无待执行任务 / 非法输入 → NULL */
    TEST_ASSERT_TRUE(patrol_task_json_from_response("{\"data\":{\"total\":0,\"rows\":[]}}") == NULL);
    TEST_ASSERT_TRUE(patrol_task_json_from_response(NULL) == NULL);
    TEST_ASSERT_TRUE(patrol_task_json_from_response("{\"code\":\"RES-0000\"}") == NULL);
}