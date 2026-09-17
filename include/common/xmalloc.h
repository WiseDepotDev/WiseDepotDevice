/**
 * 内存管理封装模块头文件
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_XMALLOC_H
#define WISE_DEPOT_XMALLOC_H

#include <stddef.h>

/**
 * 分配内存，失败时自动退出程序
 *
 * @param size 需要分配的字节数
 * @return 指向分配内存的指针
 */
void *xmalloc(size_t size);

/**
 * 分配并清零内存，失败时自动退出程序
 *
 * @param nmemb 元素个数
 * @param size 每个元素的大小
 * @return 指向分配内存的指针
 */
void *xcalloc(size_t nmemb, size_t size);

/**
 * 重新分配内存，失败时自动退出程序
 *
 * @param ptr 原指针
 * @param size 新大小
 * @return 指向新分配内存的指针
 */
void *xrealloc(void *ptr, size_t size);

/**
 * 释放内存
 *
 * @param ptr 指向需要释放的内存
 */
void xfree(void *ptr);

/**
 * 复制字符串，失败时自动退出程序
 *
 * @param s 源字符串
 * @return 新分配的字符串副本
 */
char *xstrdup(const char *s);

/**
 * 获取当前已分配但未释放的内存块数量（用于泄漏检测）
 *
 * @return 内存块计数
 */
long xmalloc_get_allocation_count(void);

#endif // WISE_DEPOT_XMALLOC_H
