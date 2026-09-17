/**
 * 日志系统模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#include "common/logger.h"
#include "common/xmalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

/* 全局日志状态 */
static FILE *g_log_fp = NULL;
static char *g_log_file = NULL;
static LogLevel g_log_level = LOG_LEVEL_INFO;

/* 日志限流相关 */
#define MAX_RECENT_LOGS 200
#define LOG_DEDUP_WINDOW 60 // 1分钟

typedef struct {
    char msg_signature[128]; // 用于比较的日志签名 (文件+行号+内容前缀)
    time_t last_time;
    int count;
} LogHistory;

static LogHistory g_recent_logs[MAX_RECENT_LOGS];
static int g_log_history_idx = 0;

int logger_init(const char *log_file, LogLevel level) {
    g_log_level = level;
    
    // 初始化日志历史
    memset(g_recent_logs, 0, sizeof(g_recent_logs));
    
    if (g_log_file) {
        xfree(g_log_file);
        g_log_file = NULL;
    }

    if (log_file != NULL) {
        g_log_file = xstrdup(log_file);
        g_log_fp = fopen(log_file, "a");
        if (!g_log_fp) {
            fprintf(stderr, "[ERROR] Failed to open log file: %s\n", log_file);
            return -1;
        }
    }
    
    return 0;
}

int logger_reopen(const char *new_log_file) {
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    
    if (new_log_file) {
        if (g_log_file) xfree(g_log_file);
        g_log_file = xstrdup(new_log_file);
    }
    
    if (g_log_file) {
        g_log_fp = fopen(g_log_file, "a");
        if (!g_log_fp) {
            fprintf(stderr, "[ERROR] Failed to reopen log file: %s\n", g_log_file);
            return -1;
        }
    }
    
    return 0;
}

void logger_close(void) {
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    if (g_log_file) {
        xfree(g_log_file);
        g_log_file = NULL;
    }
}

static const char *get_level_str(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

void logger_log(LogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < g_log_level) return;
    
    time_t now = time(NULL);
    
    va_list args;
    char buffer[1024];
    
    // Format the message first
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    // Rate limiting / Deduplication
    char signature[128];
    // Use smaller limit to ensure we don't trigger warning, and handle truncation gracefully
    // The warning happens because file+line+buffer could exceed 128 bytes.
    // We intentionally truncate for the signature.
    int sig_len = snprintf(signature, sizeof(signature), "%s:%d:%s", file, line, buffer);
    if (sig_len < 0) { 
        // Error handling
        signature[0] = '\0';
    }
    // No need to check for truncation, snprintf guarantees null-termination if size > 0
    
    int found_idx = -1;
    for (int i = 0; i < MAX_RECENT_LOGS; i++) {
        if (g_recent_logs[i].last_time > 0 && 
            strncmp(g_recent_logs[i].msg_signature, signature, sizeof(signature) - 1) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx >= 0) {
        if (now - g_recent_logs[found_idx].last_time < LOG_DEDUP_WINDOW) {
            // Duplicate within window, suppress
            g_recent_logs[found_idx].count++;
            return; 
        } else {
            // Expired, reset and print (maybe print "Repeated X times" before?)
            g_recent_logs[found_idx].last_time = now;
            g_recent_logs[found_idx].count = 0;
        }
    } else {
        // New log, add to history
        int insert_idx = g_log_history_idx;
        g_recent_logs[insert_idx].last_time = now;
        g_recent_logs[insert_idx].count = 0;
        
        // Copy signature with size check to avoid warning
        size_t dest_size = sizeof(g_recent_logs[insert_idx].msg_signature);
        size_t copy_len = strlen(signature);
        if (copy_len >= dest_size) {
            copy_len = dest_size - 1;
        }
        memcpy(g_recent_logs[insert_idx].msg_signature, signature, copy_len);
        g_recent_logs[insert_idx].msg_signature[copy_len] = '\0';
        
        g_log_history_idx = (g_log_history_idx + 1) % MAX_RECENT_LOGS;
    }

    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Output to console
    fprintf(stdout, "[%s] [%-5s] [%s:%d] %s\n", time_str, get_level_str(level), file, line, buffer);
    
    // Output to file if configured
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s] [%-5s] [%s:%d] %s\n", time_str, get_level_str(level), file, line, buffer);
        fflush(g_log_fp);
    }
}
