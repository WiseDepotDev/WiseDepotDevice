/**
 * 设备信息模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-06
 */

#include "common/device_info.h"
#include "common/xmalloc.h"
#include "common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

/* 全局缓存 */
static device_info_t *g_info = NULL;

/* 简单的字符串修剪函数 */
static char *trim_quotes(char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len-1] == '"') {
        s[len-1] = '\0';
        return s + 1;
    }
    return s;
}

static char *read_file_line(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    
    char buf[256];
    if (fgets(buf, sizeof(buf), fp)) {
        char *p = strchr(buf, '\n');
        if (p) *p = '\0';
        fclose(fp);
        return xstrdup_try(buf);
    }
    
    fclose(fp);
    return NULL;
}

static char *get_os_name(void) {
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *val = line + 12;
                char *p = strchr(val, '\n');
                if (p) *p = '\0';
                fclose(fp);
                return xstrdup_try(trim_quotes(val));
            }
        }
        fclose(fp);
    }
    
    // Fallback to uname sysname
    struct utsname uts;
    if (uname(&uts) == 0) {
        return xstrdup_try(uts.sysname);
    }
    
    return xstrdup_try("Linux (Unknown)");
}

static char *get_model_name(void) {
    // Try DMI (x86)
    char *model = read_file_line("/sys/class/dmi/id/product_name");
    if (model) return model;
    
    // Try Device Tree (ARM)
    model = read_file_line("/sys/firmware/devicetree/base/model");
    if (model) return model;
    
    return xstrdup_try("WiseDevice-Generic");
}

const device_info_t *device_info_get(void) {
    if (g_info) return g_info;
    
    g_info = (device_info_t *)xcalloc_try(1, sizeof(device_info_t));
    if (!g_info) {
        LOG_ERROR("分配设备信息结构失败（内存不足）");
        return NULL;
    }
    
    // 1. Get OS Name
    g_info->os_name = get_os_name();
    if (!g_info->os_name) {
        device_info_free();
        return NULL;
    }
    
    // 2. Get Kernel Version
    struct utsname uts;
    if (uname(&uts) == 0) {
        g_info->kernel_ver = xstrdup_try(uts.release);
    } else {
        g_info->kernel_ver = xstrdup_try("unknown");
    }
    
    if (!g_info->kernel_ver) {
        device_info_free();
        return NULL;
    }

    // 3. Get Model
    g_info->model = get_model_name();
    if (!g_info->model) {
        device_info_free();
        return NULL;
    }
    
    // 4. Serial No (Optional, usually same as device_id in config, skip for now or implement if needed)
    // For now, we leave it NULL, application layer can fill it if needed
    
    LOG_INFO("Device Info Detected: OS=%s, Kernel=%s, Model=%s", 
             g_info->os_name, g_info->kernel_ver, g_info->model);
             
    return g_info;
}

void device_info_free(void) {
    if (g_info) {
        xfree(g_info->os_name);
        xfree(g_info->kernel_ver);
        xfree(g_info->model);
        xfree(g_info->serial_no);
        xfree(g_info);
        g_info = NULL;
    }
}
