#ifndef WISE_DEPOT_UTILS_H
#define WISE_DEPOT_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Convert bytes to hex string
 *
 * @param bytes Input byte array
 * @param len Length of input bytes
 * @param out Output string buffer (must be at least len * 2 + 1 bytes)
 */
static inline void bytes_to_hex(const uint8_t *bytes, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02X", bytes[i]);
    }
    out[len * 2] = '\0';
}

#endif // WISE_DEPOT_UTILS_H
