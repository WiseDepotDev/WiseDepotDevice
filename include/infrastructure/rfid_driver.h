#ifndef WISE_DEPOT_RFID_DRIVER_H
#define WISE_DEPOT_RFID_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* P4-09：标签值对象已下沉到 domain（include/domain/tag.h），驱动层反向引用它 ——
 * 依赖方向为 infrastructure → domain，domain 不再依赖 infrastructure。 */
#include "domain/tag.h"
#include "common/wd_error.h"

// Configuration
typedef struct {
    char serial_port[64];
    int baudrate;
    int timeout_ms;
    int max_retries;
    int  address;       /**< P4-12：读头地址 0x00-0xFE；0xFF = 广播（默认，保持既有行为） */
    int  power_dbm;     /**< P4-12：初始化时下发的功率（dBm，0-33）；0 = 不下发 */
    bool legacy_frames; /**< P4-12：true = 沿用旧帧路径（广播地址、readData 旧布局、不下发功率） */
} rfid_config_t;

// Initialize the RFID reader
wd_error_t rfid_init(const rfid_config_t *config);

// Close the RFID reader
void rfid_close(void);

// Set Reader to Polling/Response Mode (Answer Mode)
int rfid_set_mode_response(void);

/** P4-12：配置化下发读头功率（CMD_SET_POWER 0x2F）。@param dbm 0-33 @return 0 成功 */
wd_error_t rfid_set_power(int dbm);

/**
 * P4-12：按 UHF 手册组请求帧（**纯函数**：不碰串口、不读全局状态，便于单测）。
 *
 * 帧格式：`Len(1) | Adr(1) | Cmd(1) | Data(...) | CRC_LSB | CRC_MSB`，
 * 其中 `Len` 不含自身（= 4 + data_len），CRC16 覆盖 `Len` 到 `Data`。
 *
 * @param out_frame 输出缓冲；@param out_cap 其容量
 * @return 帧总字节数（5 + data_len）；参数非法或缓冲不足返回 0
 */
size_t rfid_build_frame(uint8_t address, uint8_t cmd, const uint8_t *data, size_t data_len,
                        uint8_t *out_frame, size_t out_cap);

// Inventory (Scan) Tags
// returns number of tags found, or negative on error
int rfid_inventory(rfid_tag_t *tags, size_t max_tags);

// Read Data from a specific tag
int rfid_read_data(const uint8_t *epc, uint8_t epc_len, 
                   uint8_t mem_bank, uint8_t start_addr, uint8_t word_count, 
                   uint8_t *out_data);

#endif // WISE_DEPOT_RFID_DRIVER_H
