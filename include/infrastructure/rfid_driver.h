#ifndef RFID_DRIVER_H
#define RFID_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// RFID Tag Structure
typedef struct {
    uint8_t epc[32]; // Max EPC length
    uint8_t epc_len; // Actual EPC length in bytes
    uint8_t tid[32]; // Max TID length
    uint8_t tid_len; // Actual TID length in bytes
    uint8_t user_data[64]; // Max User Data
    uint8_t user_len;
    int8_t rssi;     // Signal strength (if available)
} rfid_tag_t;

// Configuration
typedef struct {
    char serial_port[64];
    int baudrate;
    int timeout_ms;
    int max_retries;
} rfid_config_t;

// Initialize the RFID reader
int rfid_init(const rfid_config_t *config);

// Close the RFID reader
void rfid_close(void);

// Set Reader to Polling/Response Mode (Answer Mode)
int rfid_set_mode_response(void);

// Inventory (Scan) Tags
// returns number of tags found, or negative on error
int rfid_inventory(rfid_tag_t *tags, size_t max_tags);

// Read Data from a specific tag
int rfid_read_data(const uint8_t *epc, uint8_t epc_len, 
                   uint8_t mem_bank, uint8_t start_addr, uint8_t word_count, 
                   uint8_t *out_data);

#endif // RFID_DRIVER_H
