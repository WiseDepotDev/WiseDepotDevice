/**
 * 系统监控模块实现
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#include "common/sys_monitor.h"
#include "common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 上一次的 CPU 统计信息 */
static unsigned long long g_last_total = 0;
static unsigned long long g_last_idle = 0;

double sys_monitor_get_cpu_usage(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        LOG_ERROR("Failed to open /proc/stat");
        return 0.0;
    }
    
    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        LOG_ERROR("Failed to read /proc/stat");
        fclose(fp);
        return 0.0;
    }
    fclose(fp);
    
    // Parse cpu line: cpu  user nice system idle iowait irq softirq steal guest guest_nice
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
    
    // Just read first numbers after "cpu"
    char *p = strstr(line, "cpu");
    if (!p) return 0.0;
    while (*p && (*p < '0' || *p > '9')) p++; // Skip non-digit
    
    // Use sscanf to parse numbers
    /* /proc/loadavg 前三个字段是 1/5/15 分钟负载，最多读 10 个数即可覆盖。 */
    int count = sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
                       
    if (count < 4) return 0.0; // At least need user, nice, system, idle
    
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    unsigned long long total_idle = idle + iowait;
    
    double usage = 0.0;
    
    // If it's not the first run and total has increased
    if (g_last_total > 0 && total > g_last_total) {
        unsigned long long diff_total = total - g_last_total;
        unsigned long long diff_idle = total_idle - g_last_idle;
        
        if (diff_total > 0) {
            usage = (double)(diff_total - diff_idle) * 100.0 / diff_total;
        }
    }
    
    // Update state
    g_last_total = total;
    g_last_idle = total_idle;
    
    return usage;
}

double sys_monitor_get_mem_usage(void) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        LOG_ERROR("Failed to open /proc/meminfo");
        return 0.0;
    }
    
    char line[256];
    unsigned long long total = 0;
    unsigned long long available = 0;
    int found_total = 0;
    int found_avail = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %llu", &total); // Removed kB check for robustness
            found_total = 1;
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %llu", &available);
            found_avail = 1;
        }
        
        if (found_total && found_avail) break;
    }
    fclose(fp);
    
    if (total == 0) return 0.0;
    
    // If MemAvailable is not found (old kernels), just return 0 for now
    if (!found_avail) {
        return 0.0; 
    }
    
    return (double)(total - available) * 100.0 / total; // NOLINT(clang-analyzer-optin.taint.TaintedDiv)：第 98 行已判 total == 0
}
