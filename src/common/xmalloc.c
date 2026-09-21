/**
 * 内存管理封装模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-06
 *
 * P4-06：本模块**不再调用 exit()**。所有接口在分配失败时返回 NULL 并写一行 stderr 日志
 * （不能在此调用 LOG_*：logger 自身依赖本模块），由调用方回滚并返回错误码，
 * 启动阶段的错误最终由 main() 返回非 0。
 */

#include "common/xmalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 全局内存块计数器 */
static long g_allocation_count = 0;

/* 故障注入：>= 0 时表示"还能成功多少次"，用于测试降级路径 */
static long g_fail_after = -1;

void xmalloc_set_fail_after(long n) {
    g_fail_after = n;
}

void xmalloc_clear_fail_after(void) {
    g_fail_after = -1;
}

static int allocation_should_fail(void) {
    if (g_fail_after < 0) return 0;
    if (g_fail_after == 0) return 1;
    g_fail_after--;
    return 0;
}

void *xmalloc_try(size_t size) {
    if (size == 0) return NULL;

    if (allocation_should_fail()) {
        fprintf(stderr, "[ERROR] Memory allocation failed (injected fault, size: %zu)\n", size);
        return NULL;
    }

    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "[ERROR] Memory allocation failed (size: %zu)\n", size);
        return NULL;
    }

    g_allocation_count++;
    return ptr;
}

void *xcalloc_try(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;

    if (allocation_should_fail()) {
        fprintf(stderr, "[ERROR] Memory allocation failed (injected fault, nmemb: %zu, size: %zu)\n", nmemb, size);
        return NULL;
    }

    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "[ERROR] Memory allocation failed (nmemb: %zu, size: %zu)\n", nmemb, size);
        return NULL;
    }

    g_allocation_count++;
    return ptr;
}

void *xrealloc_try(void *ptr, size_t size) {
    if (size == 0) {
        if (ptr) xfree(ptr);
        return NULL;
    }

    if (allocation_should_fail()) {
        fprintf(stderr, "[ERROR] Memory reallocation failed (injected fault, size: %zu)\n", size);
        return NULL; /* 注意：原指针仍有效，调用方需自行释放 */
    }

    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "[ERROR] Memory reallocation failed (size: %zu)\n", size);
        return NULL;
    }

    /* realloc 不改变块数量（ptr 非空时是同一个块换了大小） */
    if (ptr == NULL) {
        g_allocation_count++;
    }

    return new_ptr;
}

void xfree(void *ptr) {
    if (ptr) {
        free(ptr);
        g_allocation_count--;
    }
}

char *xstrdup_try(const char *s) {
    if (!s) return NULL;

    size_t len = strlen(s);
    char *new_s = (char *)xmalloc_try(len + 1);
    if (!new_s) return NULL; /* xmalloc_try 已记日志 */

    memcpy(new_s, s, len + 1);
    return new_s;
}

long xmalloc_get_allocation_count(void) {
    return g_allocation_count;
}
