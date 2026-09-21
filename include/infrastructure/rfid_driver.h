#ifndef RFID_DRIVER_H
#define RFID_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* P4-09：标签值对象已下沉到 domain（include/domain/tag.h），驱动层反向引用它 ——
 * 依赖方向为 infrastructure → domain，domain 不再依赖 infrastructure。 */
#include "domain/tag.h"

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
