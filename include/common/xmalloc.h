/**
 * 内存管理封装模块头文件
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-06
 *
 * P4-06：全部接口为"可失败"语义（失败返回 NULL），**库内部不再 exit()**。
 * 失败时向 stderr 打一行日志（此处不能调用 LOG_*：logger 模块本身依赖本模块，会形成循环依赖），
 * 由调用方回滚并返回错误码；启动阶段的失败最终由 main() 返回非 0。
 */

#ifndef WISE_DEPOT_XMALLOC_H
#define WISE_DEPOT_XMALLOC_H

#include <stddef.h>

/**
 * 分配内存（失败返回 NULL 并记日志）
 *
 * @param size 需要分配的字节数
 * @return 指针，失败返回 NULL
 */
void *xmalloc_try(size_t size);

/**
 * 分配并清零内存（失败返回 NULL 并记日志）
 *
 * @param nmemb 元素个数
 * @param size 每个元素的大小
 * @return 指针，失败返回 NULL
 */
void *xcalloc_try(size_t nmemb, size_t size);

/**
 * 重新分配内存（失败返回 NULL 并记日志；size 为 0 时等价于释放并返回 NULL）
 *
 * @param ptr 原指针
 * @param size 新大小
 * @return 指针，失败返回 NULL
 */
void *xrealloc_try(void *ptr, size_t size);

/**
 * 复制字符串（失败返回 NULL 并记日志）
 *
 * @param s 源字符串
 * @return 新分配的字符串副本，失败返回 NULL
 */
char *xstrdup_try(const char *s);

/**
 * 释放内存（NULL 安全）
 *
 * @param ptr 指向需要释放的内存
 */
void xfree(void *ptr);

/**
 * 故障注入（仅用于测试/验证降级路径）
 *
 * 令第 (n+1) 次分配请求失败（n 从 0 计），用于验证业务路径是否真的"不 exit、能回滚"。
 *
 * @param n 允许成功的分配次数；-1 表示关闭注入
 */
void xmalloc_set_fail_after(long n);

/** 关闭故障注入 */
void xmalloc_clear_fail_after(void);

/**
 * 获取当前已分配但未释放的内存块数量（用于泄漏检测）
 *
 * @return 内存块计数
 */
long xmalloc_get_allocation_count(void);

#endif // WISE_DEPOT_XMALLOC_H
