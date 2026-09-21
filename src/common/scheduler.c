/**
 * 任务调度器模块实现
 *
 * @author xingchentye
 * @version 0.3.0
 * @since 2026-03-06
 */

#include "common/scheduler.h"
#include "common/xmalloc.h"
#include "common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

/* 全局调度器状态 */
static struct {
    scheduler_task_t *head;
    volatile int running;
    unsigned int default_resolution;
} g_scheduler;

/* 获取当前时间戳 (毫秒) */
static unsigned long long get_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        LOG_ERROR("Failed to get monotonic time");
        return 0;
    }
    return (unsigned long long)(ts.tv_sec) * 1000 + (ts.tv_nsec / 1000000);
}

/* 简单的信号处理 */
static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        LOG_INFO("Scheduler received signal %d, stopping...", sig);
        g_scheduler.running = 0;
    }
}

int scheduler_init(void) {
    g_scheduler.head = NULL;
    g_scheduler.running = 0;
    g_scheduler.default_resolution = 1000;
    return 0;
}

int scheduler_add_task(const char *name, scheduler_task_callback_t callback, void *context, unsigned int interval_ms) {
    if (!name || !callback || interval_ms == 0) return -1;
    
    scheduler_task_t *new_task = (scheduler_task_t *)xcalloc_try(1, sizeof(scheduler_task_t));
    if (!new_task) {
        LOG_ERROR("分配调度任务失败（内存不足）");
        return -1;
    }
    new_task->name = xstrdup_try(name);
    if (!new_task->name) {
        LOG_ERROR("分配调度任务名失败（内存不足）");
        xfree(new_task);
        return -1;
    }
    new_task->callback = callback;
    new_task->context = context;
    new_task->interval_ms = interval_ms;
    new_task->last_run = 0; // Initialize as 0 to run immediately or wait?
    // Let's set it to current time to run after interval
    new_task->last_run = get_time_ms();
    
    // Add to list (head insert for simplicity)
    new_task->next = g_scheduler.head;
    g_scheduler.head = new_task;
    
    LOG_INFO("scheduler_task_t added: %s (interval: %dms)", name, interval_ms);
    return 0;
}

void scheduler_run(unsigned int resolution_ms) {
    if (resolution_ms == 0) resolution_ms = g_scheduler.default_resolution;
    
    g_scheduler.running = 1;
    
    // Register signal handlers for clean shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    LOG_INFO("Scheduler started (resolution: %dms)", resolution_ms);
    
    while (g_scheduler.running) {
        unsigned long long now = get_time_ms();
        if (now == 0) {
            // Clock error, break to avoid busy loop if persistent
            LOG_ERROR("Clock error in scheduler loop");
            break; 
        }
        
        scheduler_task_t *curr = g_scheduler.head;
        while (curr) {
            if (now >= curr->last_run + curr->interval_ms) {
                // Time to run task
                // LOG_DEBUG("Running task: %s", curr->name);
                curr->callback(curr->context);
                
                // Update last_run
                // Option 1: last_run = now (drifts if task is slow)
                // Option 2: last_run += interval (catches up if slow, but might burst)
                // For this simple scheduler, use Option 1 to avoid burst
                curr->last_run = get_time_ms();
            }
            curr = curr->next;
        }
        
        // Sleep for resolution
        // Use nanosleep for better precision than sleep()
        struct timespec req, rem;
        req.tv_sec = resolution_ms / 1000;
        req.tv_nsec = (resolution_ms % 1000) * 1000000;
        
        while (nanosleep(&req, &rem) == -1) {
            if (errno == EINTR) {
                if (!g_scheduler.running) break; // Signal received
                req = rem; // Continue remaining sleep
            } else {
                break;
            }
        }
    }
    
    LOG_INFO("Scheduler stopped");
}

void scheduler_stop(void) {
    g_scheduler.running = 0;
}

void scheduler_destroy(void) {
    scheduler_task_t *curr = g_scheduler.head;
    while (curr) {
        scheduler_task_t *next = curr->next;
        xfree(curr->name);
        xfree(curr);
        curr = next;
    }
    g_scheduler.head = NULL;
}
