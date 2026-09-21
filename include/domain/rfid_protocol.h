#ifndef WISE_DEPOT_RFID_PROTOCOL_H
#define WISE_DEPOT_RFID_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_FRAME_SIZE 256

typedef struct {
    uint8_t address;
    uint8_t cmd;
    uint8_t status;
    uint8_t *data;
    size_t data_len;
} rfid_response_t;

// Parse a raw response buffer into a structured response
// Returns 0 on success, -1 on error
int rfid_parse_response(const uint8_t *buffer, size_t len, rfid_response_t *out_response);

// Free the data buffer in response if allocated
void rfid_free_response(rfid_response_t *response);

#endif // WISE_DEPOT_RFID_PROTOCOL_H
