/**
 * 加密工具模块头文件
 *
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_CRYPTO_H
#define WISE_DEPOT_CRYPTO_H

#include <stddef.h>

/**
 * @brief 生成密码学安全的随机十六进制串（用于请求签名 nonce）。
 *
 * 数据源：getrandom(2) → /dev/urandom；两者都不可用时返回 0，调用方必须处理。
 * **不得**退化成 rand()——nonce 可预测会让签名可被重放（P4-14 clang-tidy cert-msc30-c）。
 *
 * @param out 输出缓冲
 * @param out_len 缓冲长度（写入 out_len-1 个十六进制字符 + NUL）
 * @return 写入的字符数；失败返回 0
 */
size_t wd_random_hex(char *out, size_t out_len);

/**
 * @brief 计算 HMAC-SHA256 摘要
 * @param key 参数
 * @param key_len 参数
 * @param data 数据段
 * @param data_len 数据段长度
 * @param output 参数
 * @return 0 成功；负值失败
 */
int hmac_sha256(const void *key, size_t key_len, const void *data, size_t data_len, void *output);

/**
 * Base64 编码
 *
 * @param data 输入数据
 * @param input_length 输入长度
 * @param output_length 输出长度指针 (可选，返回编码后长度)
 * @return 编码后的字符串 (需调用 xfree 释放)，失败返回 NULL
 */
char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);

#endif // WISE_DEPOT_CRYPTO_H
