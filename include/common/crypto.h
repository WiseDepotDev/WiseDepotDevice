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
 * 计算 HMAC-SHA256
 * 使用 Linux AF_ALG 接口 (无需第三方库)
 *
 * @param key 密钥
 * @param key_len 密钥长度
 * @param data 数据
 * @param data_len 数据长度
 * @param output 输出缓冲区 (至少 32 字节)
 * @return 0 成功，-1 失败
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
