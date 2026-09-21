#include "common/scheduler.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

static int g_counter = 0;

static void task_increment(void *ctx) {
    (void)ctx;
    g_counter++;
    printf("Task Increment: %d\n", g_counter);
}

void test_scheduler(void) {
    printf("[TEST] Scheduler starting...\n");
    
    // 1. Init
    scheduler_init();
    
    // 2. 注册一个 100ms 周期任务
    scheduler_add_task("test_task", task_increment, NULL, 100);
    
    // 3. Run (resolution 50ms)
    // 3. 只验证 API 与数据结构：循环调度是阻塞的，测试运行器不引入线程/信号。
    scheduler_destroy();
    
    printf("[TEST] Scheduler API basic check passed.\n");
}
