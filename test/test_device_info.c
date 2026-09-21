#include "common/device_info.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_device_info(void) {
    // 1. Get info (lazy init)
    const device_info_t *info = device_info_get();
    
    assert(info != NULL);
    assert(info->os_name != NULL);
    assert(info->kernel_ver != NULL);
    assert(info->model != NULL);
    
    printf("[INFO] OS: %s\n", info->os_name);
    printf("[INFO] Kernel: %s\n", info->kernel_ver);
    printf("[INFO] Model: %s\n", info->model);
    
    // 2. Get again (should be same pointer)
    const device_info_t *info2 = device_info_get();
    assert(info == info2);

    // free 之后不能再读 info 的字段（use-after-free），先做内容快照
    char *os_snapshot = strdup(info->os_name);
    char *kernel_snapshot = strdup(info->kernel_ver);
    char *model_snapshot = strdup(info->model);
    assert(os_snapshot && kernel_snapshot && model_snapshot);

    // 3. Free，模拟 device_stop 的清理
    device_info_free();

    // 4. 再次获取应重新初始化。
    // 注意：这里**不能**断言 info3 != info —— free 之后立刻分配同尺寸内存时，
    // glibc tcache 会原样返回刚释放的块，指针相等属正常（旧断言因此在 tcache 下必失败）。
    // 断言的正确目标是"重建后内容有效且与首次一致"。
    const device_info_t *info3 = device_info_get();
    assert(info3 != NULL);
    assert(info3->os_name != NULL);
    assert(info3->kernel_ver != NULL);
    assert(info3->model != NULL);
    assert(strcmp(info3->os_name, os_snapshot) == 0);
    assert(strcmp(info3->kernel_ver, kernel_snapshot) == 0);
    assert(strcmp(info3->model, model_snapshot) == 0);

    free(os_snapshot);
    free(kernel_snapshot);
    free(model_snapshot);

    device_info_free();
}
