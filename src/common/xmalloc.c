/**
 * 内存管理封装模块实现
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#include "common/xmalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 全局内存块计数器 */
static long g_allocation_count = 0;

void *xmalloc(size_t size) {
    if (size == 0) return NULL;
    
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "[ERROR] Memory allocation failed (size: %zu)\n", size);
        exit(EXIT_FAILURE);
    }
    
    g_allocation_count++;
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) return NULL;
    
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "[ERROR] Memory allocation failed (nmemb: %zu, size: %zu)\n", nmemb, size);
        exit(EXIT_FAILURE);
    }
    
    g_allocation_count++;
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    if (size == 0) {
        if (ptr) xfree(ptr);
        return NULL;
    }
    
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "[ERROR] Memory reallocation failed (size: %zu)\n", size);
        exit(EXIT_FAILURE);
    }
    
    // realloc does not increase allocation count if ptr != NULL, it just changes the size
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

char *xstrdup(const char *s) {
    if (!s) return NULL;
    
    size_t len = strlen(s);
    char *new_s = xmalloc(len + 1);
    strcpy(new_s, s);
    
    return new_s;
}

long xmalloc_get_allocation_count(void) {
    return g_allocation_count;
}
