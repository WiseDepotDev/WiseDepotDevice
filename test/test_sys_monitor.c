#include "common/sys_monitor.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

void test_sys_monitor(void) {
    // 1. Memory
    double mem = sys_monitor_get_mem_usage();
    printf("Memory Usage: %.2f%%\n", mem);
    assert(mem >= 0.0 && mem <= 100.0);
    
    // 2. CPU
    // First call returns 0.0 usually
    double cpu1 = sys_monitor_get_cpu_usage();
    printf("CPU Usage (1): %.2f%%\n", cpu1);
    assert(cpu1 >= 0.0 && cpu1 <= 100.0);
    
    // Simulate some work or sleep
    usleep(100000); // 100ms
    
    // Second call should return diff
    double cpu2 = sys_monitor_get_cpu_usage();
    printf("CPU Usage (2): %.2f%%\n", cpu2);
    assert(cpu2 >= 0.0 && cpu2 <= 100.0);
}
