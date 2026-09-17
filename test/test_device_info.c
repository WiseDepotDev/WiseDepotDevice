#include "common/device_info.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_device_info(void) {
    // 1. Get info (lazy init)
    const DeviceInfo *info = device_info_get();
    
    assert(info != NULL);
    assert(info->os_name != NULL);
    assert(info->kernel_ver != NULL);
    assert(info->model != NULL);
    
    printf("[INFO] OS: %s\n", info->os_name);
    printf("[INFO] Kernel: %s\n", info->kernel_ver);
    printf("[INFO] Model: %s\n", info->model);
    
    // 2. Get again (should be same pointer)
    const DeviceInfo *info2 = device_info_get();
    assert(info == info2);
    
    // 3. Free is called in device_stop usually, but we can call it here for test cleanup
    // Note: device_info_free handles global pointer
    device_info_free();
    
    // 4. Get again (should re-init)
    const DeviceInfo *info3 = device_info_get();
    assert(info3 != NULL);
    assert(info3 != info); // Should be new allocation
    
    device_info_free();
}
