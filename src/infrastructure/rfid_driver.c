#include "infrastructure/rfid_driver.h"
#include "common/logger.h"
#include "common/wd_error.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>

#define RFID_FRAME_MAX_LEN 256
#define RFID_PRESET_VALUE 0xFFFF
#define RFID_POLYNOMIAL 0x8408

// Global file descriptor for the serial port
static int serial_fd = -1;
static rfid_config_t current_config;

// CRC16 Calculation from Manual
static uint16_t calculate_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = RFID_PRESET_VALUE;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ RFID_POLYNOMIAL;
            } else {
                crc = (crc >> 1);
            }
        }
    }
    return crc;
}

// Low-level serial read with timeout
static int serial_read(uint8_t *buffer, size_t len, int timeout_ms) {
    if (serial_fd < 0) return WD_ERR_STATE;

    size_t total_read = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (total_read < len) {
        ssize_t n = read(serial_fd, buffer + total_read, len - total_read);
        if (n > 0) {
            total_read += n;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            return WD_ERR_IO; // Error
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) {
            break; // Timeout
        }
        
        // Small sleep to avoid busy loop
        usleep(1000); 
    }
    return total_read;
}

// Send command and receive response
static int send_command(uint8_t addr, uint8_t cmd, const uint8_t *data, size_t data_len, uint8_t *response, size_t max_resp_len) {
    if (serial_fd < 0) return WD_ERR_STATE;

    uint8_t frame[RFID_FRAME_MAX_LEN];
    size_t frame_len = 0;

    // Build Request Frame
    // Len(1) | Adr(1) | Cmd(1) | Data(...) | CRC_LSB | CRC_MSB
    // Len = 1 (Adr) + 1 (Cmd) + data_len + 2 (CRC) = 4 + data_len
    
    if (4 + data_len > RFID_FRAME_MAX_LEN) return WD_ERR_PARAM;

    frame[0] = (uint8_t)(4 + data_len); // Len doesn't include itself
    frame[1] = addr;
    frame[2] = cmd;
    if (data && data_len > 0) {
        memcpy(&frame[3], data, data_len);
    }
    
    // Calculate CRC from Len to Data
    uint16_t crc = calculate_crc16(frame, 3 + data_len);
    frame[3 + data_len] = (uint8_t)(crc & 0xFF);
    frame[4 + data_len] = (uint8_t)((crc >> 8) & 0xFF);
    
    frame_len = 5 + data_len; // Total bytes to send (including Len byte)

    // Clear input buffer
    tcflush(serial_fd, TCIFLUSH);

    // Write
    if (write(serial_fd, frame, frame_len) != (ssize_t)frame_len) {
        LOG_ERROR("Failed to write to serial port");
        return WD_ERR_IO;
    }

    // Read Response
    // Response: Len(1) | Adr(1) | reCmd(1) | Status(1) | Data(...) | CRC_LSB | CRC_MSB
    // First read Len
    uint8_t resp_len_byte;
    if (serial_read(&resp_len_byte, 1, current_config.timeout_ms) != 1) {
        LOG_WARN("Timeout waiting for response length");
        return WD_ERR_TIMEOUT; // Timeout
    }

    /* 长度合法性（P4-01）。三条理由说明旧判断 `resp_len_byte > max_resp_len` 既无用也不足：
     * 1. resp_len_byte 是 uint8_t（最大 255），当缓冲容量为 RFID_FRAME_MAX_LEN(256) 时该条件恒为假——是死判断；
     * 2. 真正危险的是**下界**：resp_len_byte == 0 时，后面的 resp_len_byte - 1 会下溢，
     *    造成 response[-1] 与超长 CRC 计算（越界读，甚至整段地址空间）；
     * 3. 帧格式 Len|Adr|reCmd|Status|Data...|CRC_LSB|CRC_MSB 决定最小有效载荷为 5 字节。
     * 正确条件 = 「不小于最小帧长」且「长度字节 + 载荷完整落在缓冲内」。 */
    enum { MIN_RESP_PAYLOAD_LEN = 5 };
    if (resp_len_byte < MIN_RESP_PAYLOAD_LEN || (size_t)resp_len_byte + 1 > max_resp_len) {
        LOG_ERROR("Invalid response length: %u (buffer capacity: %zu)",
                  (unsigned)resp_len_byte, max_resp_len);
        return WD_ERR_PROTOCOL;
    }
    /* 边界不变式：此后 response[0..resp_len_byte] 全部合法 */
    assert((size_t)resp_len_byte + 1 <= max_resp_len);

    response[0] = resp_len_byte;
    // Read the rest: resp_len_byte bytes (since Len byte excludes itself)
    int n = serial_read(&response[1], resp_len_byte, current_config.timeout_ms);
    if (n != resp_len_byte) {
        LOG_WARN("Incomplete response");
        return WD_ERR_TIMEOUT;
    }
    assert(n <= (int)max_resp_len - 1); /* 载荷写入的是 response[1..resp_len_byte] */

    // Verify CRC
    uint16_t calc_crc = calculate_crc16(response, resp_len_byte - 1); // Calculate up to Data end
    
    // Note: Manual says "LSB-CRC16 | MSB-CRC16" at the end.
    // The CRC calculation function provided in manual returns uint16.
    // Manual says: "upper computer... calculate CRC16... result 0x0000 indicates correct"
    // Wait, the manual code snippet shows checking if crc == 0x0000 if we include the CRC bytes in calculation?
    // "上位机收到数据的时候，只要把收到的数据按以上算法进行计算CRC16，结果为0x0000表明数据正确。"
    // This usually implies that if you run CRC over the whole frame INCLUDING the CRC bytes (if they are appended correctly), you get 0.
    // Let's check the code:
    // If we run calculate_crc16 on [Len ... Data ... CRC_LSB, CRC_MSB], does it result in 0?
    // The provided C code just calculates CRC over a buffer.
    // Typically: CRC(Data + CRC) == 0.
    // Let's try to verify this assumption later. For now, let's recalculate and compare.
    
    uint16_t embedded_crc = response[resp_len_byte - 1] | (response[resp_len_byte] << 8); // LSB first in stream
    if (calc_crc != embedded_crc) {
        // Try the "CRC of whole frame == 0" method just in case
        uint16_t full_crc = calculate_crc16(response, resp_len_byte + 1);
        if (full_crc != 0) {
             LOG_ERROR("CRC mismatch: calc=0x%04X, recv=0x%04X", calc_crc, embedded_crc);
             return WD_ERR_CRC;
        }
    }

    return resp_len_byte + 1; // Total length
}

wd_error_t rfid_init(const rfid_config_t *config) {
    if (!config) return WD_ERR_PARAM;
    current_config = *config;

    serial_fd = open(config->serial_port, O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_fd < 0) {
        LOG_ERROR("Error opening %s: %s", config->serial_port, strerror(errno));
        return WD_ERR_IO;
    }

    struct termios tty;
    if (tcgetattr(serial_fd, &tty) != 0) {
        LOG_ERROR("Error from tcgetattr: %s", strerror(errno));
        close(serial_fd);
        serial_fd = -1;
        return WD_ERR_IO;
    }

    cfsetospeed(&tty, B57600); // Default speed
    cfsetispeed(&tty, B57600);

    // Override if baudrate is standard (simplified for now)
    if (config->baudrate == 115200) {
        cfsetospeed(&tty, B115200);
        cfsetispeed(&tty, B115200);
    }

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_cflag |= (CLOCAL | CREAD);                // ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);              // shut off parity
    tty.c_cflag &= ~CSTOPB;                         // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                        // no hardware flow control

    tty.c_iflag &= ~IGNBRK;                         // disable break processing
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);         // shut off xon/xoff ctrl

    tty.c_lflag = 0;                                // no signaling chars, no echo,
                                                    // no canonical processing
    tty.c_oflag = 0;                                // no remapping, no delays

    tty.c_cc[VMIN]  = 0;            // read doesn't block
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
        LOG_ERROR("Error from tcsetattr: %s", strerror(errno));
        close(serial_fd);
        serial_fd = -1;
        return WD_ERR_IO;
    }

    return 0;
}

void rfid_close(void) {
    if (serial_fd >= 0) {
        close(serial_fd);
        serial_fd = -1;
    }
}

int rfid_set_mode_response(void) {
    // CMD_SET_WORK_MODE (0x35)
    // Data: 
    // 0: ReadMode (0=Answer, 1=Active, 2=Trigger) -> 0x00
    // 1: ModeState (Bit0=0:Gen2, Bit1=1:RS232) -> 0x02
    // 2: MemInven (0x01=EPC)
    // 3: FirstAddr (0x00)
    // 4: WordNum (0x06, e.g. 12 bytes) - Cannot be 0
    // 5: TagTime (0x05=500ms)
    
    uint8_t data[6] = {0x00, 0x02, 0x01, 0x00, 0x06, 0x05};
    uint8_t resp[RFID_FRAME_MAX_LEN];
    
    int ret = send_command(0xFF, 0x35, data, 6, resp, sizeof(resp));
    if (ret < 0) return ret;
    
    // Check status
    // Response: Len | Adr | reCmd | Status | ...
    if (resp[3] != 0x00) {
        LOG_ERROR("Set mode failed with status: 0x%02X", resp[3]);
        return WD_ERR_PROTOCOL;
    }
    return 0;
}

int rfid_inventory(rfid_tag_t *tags, size_t max_tags) {
    // CMD_INVENTORY (0x01)
    // Data: AdrTID(1), LenTID(1) - Optional, assume EPC inventory if not present or empty
    // Let's send basic inventory command with no data for EPC
    
    uint8_t resp[RFID_FRAME_MAX_LEN];
    int count = 0;
    
    int ret = send_command(0xFF, 0x01, NULL, 0, resp, sizeof(resp));
    if (ret < 0) return ret;
    
    uint8_t status = resp[3];
    if (status == 0xFB) { // No tags
        return 0;
    }
    if (status != 0x01 && status != 0x03 && status != 0x04) {
        // 0x01: Complete, 0x03: More data, 0x04: Memory full
        LOG_ERROR("Inventory failed status: 0x%02X", status);
        return WD_ERR_PROTOCOL;
    }
    
    // Parse tags
    // Data starts at resp[4]
    // Format: Num(1) | [EPC_Len(1) | EPC_Data(...)]...
    
    int data_idx = 4;
    int num_in_packet = resp[data_idx++];
    
    for (int i = 0; i < num_in_packet && count < (int)max_tags; i++) {
        // Ensure we have at least 1 byte for length
        if (data_idx >= ret) break;
        
        uint8_t epc_len = resp[data_idx++];
        if (epc_len > 32) epc_len = 32; // Cap length
        
        // Ensure we have enough data for EPC
        if (data_idx + epc_len > ret) {
            LOG_WARN("Incomplete EPC data in packet");
            break;
        }
        
        memcpy(tags[count].epc, &resp[data_idx], epc_len);
        tags[count].epc_len = epc_len;
        
        // Clear TID as it's not present in basic inventory response
        tags[count].tid_len = 0;
        
        data_idx += epc_len;
        count++;
    }
    
    // If status is 0x03 or 0x04, we might need to fetch more?
    // Manual says: "If status is 0x03, there is more data."
    // But how to get it? The reader just sends another packet?
    // Or do we need to poll again?
    // "Reader will return response... if multiple messages... will send separately"
    // Since we use a request-response model, usually we just read again from serial port without sending command?
    // The `serial_read` function above reads ONE response frame. 
    // If the reader sends multiple frames for one command, we need to handle that.
    // However, for simplicity in this "Request/Response" implementation, 
    // we might need a loop to check if more data is coming if status is 0x03.
    
    // For now, let's just return what we got.
    return count;
}

int rfid_read_data(const uint8_t *epc, uint8_t epc_len, 
                   uint8_t mem_bank, uint8_t start_addr, uint8_t word_count, 
                   uint8_t *out_data) {
    // CMD_READ_DATA (0x02)
    // Data: ENum(1) | EPC(N) | Mem(1) | WordPtr(1) | Num(1) | Pwd(4) | [Mask...]
    
    uint8_t data[64];
    int idx = 0;
    
    data[idx++] = epc_len / 2; // ENum is in Words (2 bytes)
    memcpy(&data[idx], epc, epc_len);
    idx += epc_len;
    
    data[idx++] = mem_bank;
    data[idx++] = start_addr;
    data[idx++] = word_count;
    
    // Password (0x00000000)
    memset(&data[idx], 0, 4);
    idx += 4;
    
    uint8_t resp[RFID_FRAME_MAX_LEN];
    int ret = send_command(0xFF, 0x02, data, idx, resp, sizeof(resp));
    if (ret < 0) return ret;
    
    if (resp[3] != 0x00) {
        LOG_ERROR("Read data failed status: 0x%02X", resp[3]);
        return WD_ERR_PROTOCOL;
    }
    
    // Success. Data starts at 4. Length is determined by packet length.
    // Len field in response = 5 + DataLen
    // So DataLen = Len - 5
    int len_field = resp[0];
    int data_len = len_field - 5;
    
    if (data_len > 0 && out_data) {
        memcpy(out_data, &resp[4], data_len);
    }
    
    return data_len;
}
