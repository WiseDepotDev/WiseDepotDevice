#ifndef WISE_DEPOT_UTILS_H
#define WISE_DEPOT_UTILS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** 输出缓冲区不足时用于表示"已被截断"的返回值语义：返回值为实际写入字符数（不含 '\0'） */
/**
 * Convert bytes to hex string（大写十六进制）
 *
 * P4-01：增加输出容量参数，且**永不越界**——最多写 (out_size - 1) 个字符并始终以 '\0' 结尾；
 * 容量不足以容纳全部字节时，按可容纳的**完整字节**截断（不会写出半个字节）。
 * 内部不再使用 sprintf（无界写），改为查表 + 手动写入。
 *
 * @param bytes    Input byte array（可为 NULL，此时输出空串）
 * @param len      Length of input bytes
 * @param out      Output buffer
 * @param out_size Capacity of out in bytes（必须 > 0）
 * @return 实际写入的十六进制字符数（不含结尾 '\0'）
 */
static inline size_t bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_size) {
    static const char hex_digits[] = "0123456789ABCDEF";

    if (out == NULL || out_size == 0) return 0;
    if (bytes == NULL) {
        out[0] = '\0';
        return 0;
    }

    size_t max_bytes = (out_size - 1) / 2; /* 每字节 2 个字符 + 结尾 '\0' */
    size_t n = (len < max_bytes) ? len : max_bytes;
    size_t written = 0;

    for (size_t i = 0; i < n; i++) {
        out[written++] = hex_digits[(bytes[i] >> 4) & 0x0F];
        out[written++] = hex_digits[bytes[i] & 0x0F];
    }
    out[written] = '\0';
    return written;
}

/**
 * 安全累加式格式化（P4-01：替代 `offset += snprintf(buf + offset, size - offset, ...)`）
 *
 * 该写法的两个隐患在旧代码里都存在：
 * 1. snprintf 返回的是**期望写入长度**，一旦发生截断，offset 会越过缓冲容量，
 *    随后 `cap - used` 在 size_t 下溢成巨大值 → 越界写；
 * 2. `buf + used` 在 used > cap 时已是越界指针（UB）。
 *
 * 本函数保证：任何情况下都不会写出缓冲区之外，且缓冲区始终以 '\0' 结尾。
 *
 * @param buf  目标缓冲区
 * @param cap  缓冲区容量（字节）
 * @param used 已用长度（不含 '\0'）
 * @param fmt  printf 风格格式串
 * @return 新的已用长度；发生截断（或已满）时返回 cap - 1
 */
static inline size_t wd_str_appendf(char *buf, size_t cap, size_t used, const char *fmt, ...) {
    if (buf == NULL || cap == 0 || fmt == NULL) return 0;
    if (used >= cap) return cap - 1; /* 已满：不再写，保持可读的 NUL 结尾 */

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + used, cap - used, fmt, ap);
    va_end(ap);

    if (n < 0) {
        buf[used] = '\0';
        return used;
    }

    size_t appended = (size_t)n;
    if (appended >= cap - used) {
        return cap - 1; /* 被截断：vsnprintf 已保证 NUL 结尾 */
    }
    return used + appended;
}

#endif // WISE_DEPOT_UTILS_H
