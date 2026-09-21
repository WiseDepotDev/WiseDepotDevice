/**
 * 加密工具模块实现
 *
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-06
 */

#include "common/crypto.h"
#include "common/xmalloc.h"
#include "common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/if_alg.h>
#include <stdint.h>

#ifndef SOL_ALG
#define SOL_ALG 279
#endif

int hmac_sha256(const void *key, size_t key_len, const void *data, size_t data_len, void *output) {
    int tfmfd = -1, opfd = -1;
    int ret = -1;
    struct sockaddr_alg sa = {
        .salg_family = AF_ALG,
        .salg_type = "hash",
        .salg_name = "hmac(sha256)"
    };

    // 1. Create socket
    tfmfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (tfmfd < 0) {
        LOG_ERROR("Failed to create AF_ALG socket");
        return -1;
    }

    // 2. Bind
    if (bind(tfmfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        LOG_ERROR("Failed to bind AF_ALG socket (hmac(sha256))");
        goto cleanup;
    }

    // 3. Set Key
    if (setsockopt(tfmfd, SOL_ALG, ALG_SET_KEY, key, key_len) < 0) {
        LOG_ERROR("Failed to set HMAC key");
        goto cleanup;
    }

    // 4. Accept (Create Op FD)
    opfd = accept(tfmfd, NULL, 0);
    if (opfd < 0) {
        LOG_ERROR("Failed to accept AF_ALG socket");
        goto cleanup;
    }

    // 5. Write Data
    if (write(opfd, data, data_len) != (ssize_t)data_len) {
        LOG_ERROR("Failed to write data to HMAC");
        goto cleanup;
    }

    // 6. Read Result (SHA256 = 32 bytes)
    if (read(opfd, output, 32) != 32) {
        LOG_ERROR("Failed to read HMAC result");
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (opfd >= 0) close(opfd);
    if (tfmfd >= 0) close(tfmfd);
    return ret;
}

// Base64 encoding table
static const char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                      'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                      'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                      'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                      'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                      'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                      'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                      '4', '5', '6', '7', '8', '9', '+', '/'};
static const int mod_table[] = {0, 2, 1};

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    size_t out_len = 4 * ((input_length + 2) / 3);
    char *encoded_data = (char *)xmalloc_try(out_len + 1);
    if (!encoded_data) {
        return NULL; /* P4-06：分配失败返回 NULL，由调用方处理 */
    }
    if (encoded_data == NULL) return NULL;

    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[out_len - 1 - i] = '=';

    encoded_data[out_len] = '\0';
    if (output_length) *output_length = out_len;

    return encoded_data;
}
