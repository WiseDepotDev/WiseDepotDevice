#include "domain/rfid_protocol.h"
#include "common/xmalloc.h"
#include "common/logger.h"
#include <string.h>

#define PRESET_VALUE 0xFFFF
#define POLYNOMIAL 0x8408

static uint16_t calculate_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = PRESET_VALUE;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ POLYNOMIAL;
            } else {
                crc = (crc >> 1);
            }
        }
    }
    return crc;
}

int rfid_parse_response(const uint8_t *buffer, size_t len, rfid_response_t *out_response) {
    if (!buffer || !out_response || len < 5) {
        return -1;
    }

    // Verify CRC
    uint16_t calc_crc = calculate_crc16(buffer, len - 2);
    uint16_t recv_crc = buffer[len - 2] | (buffer[len - 1] << 8);

    if (calc_crc != recv_crc) {
        LOG_ERROR("CRC mismatch: calc=0x%04X, recv=0x%04X", calc_crc, recv_crc);
        return -1;
    }

    out_response->address = buffer[1];
    out_response->cmd = buffer[2];
    out_response->status = buffer[3];

    // Data starts at index 4. Length is total len - 5 (header + crc)
    // buffer[0] is length of packet excluding itself. So total packet len is buffer[0] + 1.
    // We assume 'len' passed here is the actual received length.
    
    // Check if buffer[0] matches len - 1
    if (buffer[0] != len - 1) {
        LOG_WARN("Length mismatch: expected %d, got %zu", buffer[0], len - 1);
        // Continue? Or fail?
    }

    size_t data_len = len - 5;
    
    // Fix for type-limits error: data_len is size_t, so it can be larger than MAX_FRAME_SIZE
    if (data_len > MAX_FRAME_SIZE) {
        LOG_ERROR("Data length too large: %zu", data_len);
        return -1;
    }

    out_response->data_len = data_len;
    if (data_len > 0) {
        out_response->data = (uint8_t *)xmalloc(data_len);
        memcpy(out_response->data, &buffer[4], data_len);
    } else {
        out_response->data = NULL;
    }

    return 0;
}

void rfid_free_response(rfid_response_t *response) {
    if (response && response->data) {
        xfree(response->data);
        response->data = NULL;
    }
}
