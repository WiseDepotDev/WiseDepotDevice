/**
 * 日志系统模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#include "common/logger.h"
#include "common/xmalloc.h"
#include "common/wd_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* 全局日志状态（P4-03：全部共享状态由 g_log_mutex 保护） */
static FILE *g_log_fp = NULL;
static char *g_log_file = NULL;
static log_level_t g_log_level = LOG_LEVEL_INFO;

/* 并发设计（P4-03）：
 * - 状态区（g_log_fp / g_log_file / g_log_level / g_recent_logs / g_log_history_idx）统一由本互斥量保护；
 * - 输出区另外用 flockfile/funlockfile 保证"整行"原子性（同进程内其它模块直接写 stdout 也不会与本模块撕裂）；
 * - **递归死锁防线**：临界区内绝不调用 logger_log()/LOG_*（去重命中、文件打开失败等分支都只做
 *   "解锁并返回"或写 stderr）。静态初始化互斥量，避免初始化次序问题。 */
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

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

int logger_init(const char *log_file, log_level_t level) {
    pthread_mutex_lock(&g_log_mutex);

    g_log_level = level;

    // 初始化日志历史
    memset(g_recent_logs, 0, sizeof(g_recent_logs));
    g_log_history_idx = 0;

    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    if (g_log_file) {
        xfree(g_log_file);
        g_log_file = NULL;
    }

    if (log_file != NULL) {
        g_log_file = xstrdup_try(log_file);
        if (!g_log_file) {
            pthread_mutex_unlock(&g_log_mutex);
            return WD_ERR_NOMEM;
        }
        g_log_fp = fopen(log_file, "a");
        if (!g_log_fp) {
            /* 注意：这里不调用 LOG_*（会在持锁状态下递归取锁） */
            fprintf(stderr, "[ERROR] Failed to open log file: %s\n", log_file);
            pthread_mutex_unlock(&g_log_mutex);
            return WD_ERR_IO;
        }
    }

    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int logger_reopen(const char *new_log_file) {
    pthread_mutex_lock(&g_log_mutex);

    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    if (new_log_file) {
        if (g_log_file) xfree(g_log_file);
        g_log_file = xstrdup_try(new_log_file);
        if (!g_log_file) {
            pthread_mutex_unlock(&g_log_mutex);
            return WD_ERR_NOMEM;
        }
    }

    int rc = 0;
    if (g_log_file) {
        g_log_fp = fopen(g_log_file, "a");
        if (!g_log_fp) {
            fprintf(stderr, "[ERROR] Failed to reopen log file: %s\n", g_log_file);
            rc = -1;
        }
    }

    pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

void logger_close(void) {
    pthread_mutex_lock(&g_log_mutex);

    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    if (g_log_file) {
        xfree(g_log_file);
        g_log_file = NULL;
    }

    pthread_mutex_unlock(&g_log_mutex);
}

static const char *get_level_str(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

void logger_log(log_level_t level, const char *file, int line, const char *fmt, ...) {
    /* 1) 先在局部缓冲完成格式化：不碰共享状态，尽量缩短临界区 */
    va_list args;
    char buffer[1024];
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    char signature[128];
    int sig_len = snprintf(signature, sizeof(signature), "%s:%d:%s", file, line, buffer);
    if (sig_len < 0) {
        signature[0] = '\0';
    }

    /* localtime 不是线程安全的（返回静态缓冲区），改用 localtime_r */
    time_t now = time(NULL);
    struct tm tm_info;
    char time_str[20];
    if (localtime_r(&now, &tm_info) != NULL) {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);
    } else {
        snprintf(time_str, sizeof(time_str), "0000-00-00 00:00:00");
    }

    /* 整行先在本地拼好：输出阶段只需一次 fputs，进一步压缩临界区 */
    char line_buf[1200];
    snprintf(line_buf, sizeof(line_buf), "[%s] [%-5s] [%s:%d] %s\n",
             time_str, get_level_str(level), file, line, buffer);

    /* 2) 临界区：级别过滤 → 去重 → 输出（临界区内不再调用 LOG_*，避免递归死锁） */
    pthread_mutex_lock(&g_log_mutex);

    if (level < g_log_level) {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

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
            pthread_mutex_unlock(&g_log_mutex);
            return;
        } else {
            g_recent_logs[found_idx].last_time = now;
            g_recent_logs[found_idx].count = 0;
        }
    } else {
        int insert_idx = g_log_history_idx;
        g_recent_logs[insert_idx].last_time = now;
        g_recent_logs[insert_idx].count = 0;

        size_t dest_size = sizeof(g_recent_logs[insert_idx].msg_signature);
        size_t copy_len = strlen(signature);
        if (copy_len >= dest_size) {
            copy_len = dest_size - 1;
        }
        memcpy(g_recent_logs[insert_idx].msg_signature, signature, copy_len);
        g_recent_logs[insert_idx].msg_signature[copy_len] = '\0';

        g_log_history_idx = (g_log_history_idx + 1) % MAX_RECENT_LOGS;
    }

    /* 输出区用 flockfile 保证整行原子（覆盖其它模块直接写 stdout 的情况） */
    flockfile(stdout);
    fputs(line_buf, stdout);
    funlockfile(stdout);

    if (g_log_fp) {
        flockfile(g_log_fp);
        fputs(line_buf, g_log_fp);
        fflush(g_log_fp);
        funlockfile(g_log_fp);
    }

    pthread_mutex_unlock(&g_log_mutex);
}
