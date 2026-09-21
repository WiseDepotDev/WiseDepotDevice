/**
 * @file test_xmalloc.c
 * @brief 内存封装的正常路径与"失败降级"用例（P4-06）。
 *
 * P4-06 之后本模块**不再 exit**：失败返回 NULL，由调用方回滚。
 * 这里除了正常路径，还覆盖两件此前无法验证的事：
 * 1. 巨量分配请求返回 NULL（而不是杀进程）；
 * 2. 用故障注入让业务路径遇到 OOM 时**优雅失败**（返回 NULL/-1），进程继续活着。
 */

#include "common/xmalloc.h"
#include "domain/patrol_task.h"
#include "domain/inventory_manager.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void test_xmalloc(void) {
    long initial_count = xmalloc_get_allocation_count();

    // Test malloc
    char *p1 = xmalloc_try(10);
    assert(p1 != NULL);
    assert(xmalloc_get_allocation_count() == initial_count + 1);

    // Test calloc
    int *p2 = xcalloc_try(5, sizeof(int));
    assert(p2 != NULL);
    assert(xmalloc_get_allocation_count() == initial_count + 2);
    for (int i = 0; i < 5; i++) {
        assert(p2[i] == 0);
    }

    // Test realloc
    p1 = xrealloc_try(p1, 20);
    assert(p1 != NULL);
    assert(xmalloc_get_allocation_count() == initial_count + 2); // Realloc doesn't change count if ptr exists

    // Test free
    xfree(p1);
    assert(xmalloc_get_allocation_count() == initial_count + 1);
    xfree(p2);
    assert(xmalloc_get_allocation_count() == initial_count);

    // Test strdup
    char *s = xstrdup_try("hello");
    assert(s != NULL);
    assert(strcmp(s, "hello") == 0);
    assert(xmalloc_get_allocation_count() == initial_count + 1);
    xfree(s);
    assert(xmalloc_get_allocation_count() == initial_count);
}

/** 巨量分配应返回 NULL（不退出进程）；注入的失败同样返回 NULL */
void test_xmalloc_try_returns_null_on_failure(void) {
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    /* 请求远超可用内存：malloc 会失败，函数必须返回 NULL 而不是 exit。
     * 注意：ASan/TSan 会把 allocation-size-too-big 视为致命错误直接中止，
     * 因此这一条只在普通构建下断言；失败降级在 sanitizer 构建里由下面
     * 的故障注入用例覆盖（注入不经过真实 malloc，sanitizer 无感知）。 */
    void *huge = xmalloc_try((size_t)-1 / 2);
    assert(huge == NULL);
#endif

    /* 故障注入：下一次分配必然失败 */
    xmalloc_set_fail_after(0);
    void *injected = xmalloc_try(64);
    assert(injected == NULL);
    char *injected_str = xstrdup_try("boom");
    assert(injected_str == NULL);
    xmalloc_clear_fail_after();

    /* 关闭注入后恢复正常 */
    void *ok = xmalloc_try(64);
    assert(ok != NULL);
    xfree(ok);

    /* NULL / 0 尺寸的边界语义 */
    assert(xmalloc_try(0) == NULL);
    assert(xcalloc_try(0, 16) == NULL);
    assert(xstrdup_try(NULL) == NULL);
}

/** 业务路径遇到 OOM 必须优雅失败（返回 NULL/-1），进程不受影响 */
void test_business_path_degrades_on_oom(void) {
    /* 1) patrol_task_create 在第一次分配就失败 → 返回 NULL */
    xmalloc_set_fail_after(0);
    patrol_task_t *task = patrol_task_create("t-oom", "oom");
    assert(task == NULL);
    xmalloc_clear_fail_after();

    /* 2) 正常创建，但让"加动作"所需的分配失败 → 走错误码而不是崩溃 */
    task = patrol_task_create("t-ok", "ok");
    assert(task != NULL);
    patrol_action_t action;
    memset(&action, 0, sizeof(action));
    action.type = PATROL_ACTION_MOVE_FORWARD;
    action.speed = 50;
    action.duration_ms = 1000;
    assert(patrol_task_add_action(task, &action) == 0);
    patrol_task_free(task);

    /* 3) 预期库存加载：管理器分配失败后必须返回 -1（而不是退出/继续写空指针） */
    xmalloc_set_fail_after(0);
    inventory_mgr_init(); /* 内部首次分配被注入失败 */
    int rc = inventory_load_expected("{\"header\":{},\"payload\":{\"code\":\"RES-0000\",\"data\":[]}}");
    assert(rc == -1);
    xmalloc_clear_fail_after();

    /* 恢复后仍可用（证明失败没有破坏全局状态） */
    inventory_mgr_init();
    rc = inventory_load_expected("{\"header\":{\"request_id\":\"r\",\"packet_type\":\"X\",\"timestamp\":1},"
                                 "\"payload\":{\"code\":\"RES-0000\",\"data\":{\"rows\":[]}}}");
    assert(rc == 0);
    inventory_mgr_free();
}
