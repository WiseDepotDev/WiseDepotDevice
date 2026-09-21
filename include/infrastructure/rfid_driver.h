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
/**
 * @brief 打开串口并初始化 RFID 读头（含可选功率下发）
 * @param config 配置
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t rfid_init(const rfid_config_t *config);

// Close the RFID reader
/**
 * @brief 关闭 RFID 串口
 */
void rfid_close(void);

// Set Reader to Polling/Response Mode (Answer Mode)
/**
 * @brief 把读头切到应答（轮询）模式
 * @return 0 成功；负值失败（见 common/wd_error.h）
 */
int rfid_set_mode_response(void);

/** P4-12：配置化下发读头功率（CMD_SET_POWER 0x2F）。@param dbm 0-33 @return 0 成功 */
/**
 * @brief 下发读头功率
 * @param dbm 功率（dBm，0-33）
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
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
/**
 * @brief 按 UHF 手册组请求帧（纯函数，可单测）
 * @param address 读头地址（0xFF 广播）
 * @param cmd 命令字
 * @param data 数据段
 * @param data_len 数据段长度
 * @param out_frame 输出帧缓冲
 * @param out_cap 输出缓冲容量
 * @return 帧总字节数；参数非法或缓冲不足返回 0
 */
size_t rfid_build_frame(uint8_t address, uint8_t cmd, const uint8_t *data, size_t data_len,
                        uint8_t *out_frame, size_t out_cap);

// Inventory (Scan) Tags
// returns number of tags found, or negative on error
/**
 * @brief 执行一次 EPC 盘点
 * @param tags 标签数组
 * @param max_tags 标签数组容量
 * @return 解析到的标签数；失败返回负错误码
 */
int rfid_inventory(rfid_tag_t *tags, size_t max_tags);

// Read Data from a specific tag
/**
 * @brief 按 EPC 读取指定存储体数据
 * @param epc 标签 EPC
 * @param epc_len EPC 字节长度
 * @param mem_bank 存储体
 * @param start_addr 起始字地址
 * @param word_count 读取字数
 * @param out_data 输出缓冲
 * @return 读取到的字节数；失败返回负错误码
 */
int rfid_read_data(const uint8_t *epc, uint8_t epc_len, 
                   uint8_t mem_bank, uint8_t start_addr, uint8_t word_count, 
                   uint8_t *out_data);

#endif // WISE_DEPOT_RFID_DRIVER_H
