/**
 * @file test_patrol_lifecycle.c
 * @brief 巡检任务生命周期与取消语义用例（P4-07）。
 *
 * 覆盖三件此前无法验证、且旧实现明确做错的事：
 * 1. **取消标志是每任务独立的**（旧实现是全局 g_cancel_flag：取消 A 会影响 B）；
 * 2. **执行中被取消 ⇒ 状态必须是 CANCELLED**，且"末个动作期间被取消"不能被写成 COMPLETED；
 * 3. 正常执行路径仍然是 COMPLETED。
 *
 * 说明：用例全部走 `patrol_task_execute` + 回调返回 1（"已处理"），因此不会触碰电机/串口等硬件；
 * 取消路径里的 `motor_stop_all()` 在控制器未初始化时只会记日志并返回 WD_ERR_STATE。
 */

#include "unity.h"
#include "common/wd_error.h"
#include "domain/patrol_task.h"
#include <assert.h>
#include <string.h>

typedef struct {
    int visited[PATROL_TASK_MAX_ACTIONS];
    int visited_count;
    uint8_t cancel_at_index; /* 在该动作索引处请求取消；0xFF 表示不取消 */
    int return_handled;      /* 回调返回值（1 = 已处理，跳过默认执行） */
} lifecycle_ctx_t;

static int lifecycle_callback(const patrol_task_t *task, uint8_t action_index, void *context) {
    lifecycle_ctx_t *ctx = (lifecycle_ctx_t *)context;
    ctx->visited[ctx->visited_count++] = action_index;

    if (action_index == ctx->cancel_at_index) {
        /* 模拟外部取消（回调拿到的是 const 指针，测试里显式去 const） */
        patrol_task_cancel((patrol_task_t *)task);
    }
    return ctx->return_handled;
}

static patrol_task_t *make_task(const char *id, uint8_t action_count) {
    patrol_task_t *task = patrol_task_create(id, id);
    assert(task != NULL);
    for (uint8_t i = 0; i < action_count; i++) {
        patrol_action_t action;
        memset(&action, 0, sizeof(action));
        action.type = PATROL_ACTION_MOVE_FORWARD;
        action.speed = 50;
        action.duration_ms = 100;
        assert(patrol_task_add_action(task, &action) == 0);
    }
    return task;
}

/** 取消标志是每任务独立的：取消 A 不得影响 B（旧实现是全局标志） */
void test_patrol_cancel_is_per_task(void) {
    patrol_task_t *a = make_task("life-a", 2);
    patrol_task_t *b = make_task("life-b", 2);

    a->status = PATROL_TASK_STATUS_RUNNING;
    TEST_ASSERT_EQUAL(0, patrol_task_cancel(a));

    TEST_ASSERT_TRUE(a->cancel_requested);
    TEST_ASSERT_FALSE(b->cancel_requested); /* 关键断言：没有全局取消状态 */

    /* 未运行的任务不允许取消（P4-11：具名错误码 WD_ERR_STATE） */
    b->status = PATROL_TASK_STATUS_PENDING;
    TEST_ASSERT_EQUAL(WD_ERR_STATE, patrol_task_cancel(b));

    patrol_task_free(a);
    patrol_task_free(b);
}

/** 执行中途被取消：返回 WD_ERR_CANCELED、状态 CANCELLED、后续动作不再执行 */
void test_patrol_execute_cancelled_midway(void) {
    patrol_task_t *task = make_task("life-cancel-mid", 3);
    lifecycle_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.visited_count = 0;
    ctx.cancel_at_index = 1;
    ctx.return_handled = 1;

    int rc = patrol_task_execute(task, lifecycle_callback, &ctx);

    TEST_ASSERT_EQUAL(WD_ERR_CANCELED, rc);
    TEST_ASSERT_EQUAL(PATROL_TASK_STATUS_CANCELLED, task->status);
    TEST_ASSERT_EQUAL(2, ctx.visited_count);      /* 只执行到索引 1 */
    TEST_ASSERT_EQUAL(0, ctx.visited[0]);
    TEST_ASSERT_EQUAL(1, ctx.visited[1]);

    patrol_task_free(task);
}

/** 末个动作期间被取消：不得被写成 COMPLETED（旧实现无条件覆盖状态） */
void test_patrol_execute_cancelled_on_last_action(void) {
    patrol_task_t *task = make_task("life-cancel-last", 2);
    lifecycle_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.visited_count = 0;
    ctx.cancel_at_index = 1; /* 最后一个动作 */
    ctx.return_handled = 1;

    int rc = patrol_task_execute(task, lifecycle_callback, &ctx);

    TEST_ASSERT_EQUAL(WD_ERR_CANCELED, rc);
    TEST_ASSERT_EQUAL(PATROL_TASK_STATUS_CANCELLED, task->status);
    TEST_ASSERT_TRUE(task->cancel_requested);

    patrol_task_free(task);
}

/** 正常执行：返回 0 且状态 COMPLETED */
void test_patrol_execute_completes_normally(void) {
    patrol_task_t *task = make_task("life-ok", 3);
    lifecycle_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.visited_count = 0;
    ctx.cancel_at_index = 0xFF; /* 不取消 */
    ctx.return_handled = 1;

    int rc = patrol_task_execute(task, lifecycle_callback, &ctx);

    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_EQUAL(PATROL_TASK_STATUS_COMPLETED, task->status);
    TEST_ASSERT_FALSE(task->cancel_requested);
    TEST_ASSERT_EQUAL(3, ctx.visited_count);

    patrol_task_free(task);
}

/** 新建任务默认未取消（字段初始化契约） */
void test_patrol_new_task_is_not_cancelled(void) {
    patrol_task_t *task = patrol_task_create("life-new", "life-new");
    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_FALSE(task->cancel_requested);
    TEST_ASSERT_EQUAL(PATROL_TASK_STATUS_PENDING, task->status);
    patrol_task_free(task);
}
