/**
 * @file test_rfid_frame.c
 * @brief P4-12：RFID 组帧纯函数与配置化读头参数的用例。
 *
 * 三件事此前无法验证，现在可以：
 * 1. 组帧是否真的符合手册布局（`Len|Adr|Cmd|Data|CRC_LSB|CRC_MSB`）——用**独立实现**的
 *    CRC16 复算比对，而不是调用被测代码自己的 CRC；
 * 2. 非法输入是否被拒绝（NULL、容量不足、超长 data_len）；
 * 3. 读头地址/功率/波特率/旧帧开关是否能被环境变量覆盖，且**越界值被忽略**而不是写进配置。
 */

#include "infrastructure/rfid_driver.h"
#include "common/config.h"
#include "unity.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** 独立复算 CRC16（预置 0xFFFF、多项式 0x8408），与实现互不依赖 */
static uint16_t ref_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void test_rfid_build_frame_matches_manual_layout(void) {
    uint8_t data[2] = {0xAA, 0xBB};
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));

    size_t n = rfid_build_frame(0x01, 0x2F, data, sizeof(data), frame, sizeof(frame));

    TEST_ASSERT_EQUAL(7, n);
    TEST_ASSERT_EQUAL(6, frame[0]);    /* Len = 4 + data_len（不含自身） */
    TEST_ASSERT_EQUAL(0x01, frame[1]); /* Adr */
    TEST_ASSERT_EQUAL(0x2F, frame[2]); /* Cmd */
    TEST_ASSERT_EQUAL(0xAA, frame[3]);
    TEST_ASSERT_EQUAL(0xBB, frame[4]);

    uint16_t crc = ref_crc16(frame, 5);
    TEST_ASSERT_EQUAL((crc & 0xFF), frame[5]);
    TEST_ASSERT_EQUAL(((crc >> 8) & 0xFF), frame[6]);

    /* 无数据帧：Len = 4，总长 5 */
    n = rfid_build_frame(0xFF, 0x01, NULL, 0, frame, sizeof(frame));
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL(4, frame[0]);
    TEST_ASSERT_EQUAL(0xFF, frame[1]);
    TEST_ASSERT_EQUAL(0x01, frame[2]);
}

void test_rfid_build_frame_rejects_bad_input(void) {
    uint8_t frame[64];
    uint8_t data[4] = {1, 2, 3, 4};

    TEST_ASSERT_EQUAL(0, rfid_build_frame(0xFF, 0x01, NULL, 1, frame, sizeof(frame))); /* data=NULL 但 len>0 */
    TEST_ASSERT_EQUAL(0, rfid_build_frame(0xFF, 0x01, data, sizeof(data), NULL, 64));   /* 无输出缓冲 */
    TEST_ASSERT_EQUAL(0, rfid_build_frame(0xFF, 0x01, data, sizeof(data), frame, 8));   /* 容量不足（需 9） */

    /* 超长：Len 是 1 字节，data_len 上限 251 */
    static uint8_t big[512];
    TEST_ASSERT_EQUAL(0, rfid_build_frame(0xFF, 0x01, big, 252, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(256, rfid_build_frame(0xFF, 0x01, big, 251, big + 256, 256));
}

void test_rfid_config_env_overrides(void) {
    setenv("WISE_RFID_ADDRESS", "0x12", 1);
    setenv("WISE_RFID_POWER", "30", 1);
    setenv("WISE_RFID_BAUDRATE", "115200", 1);
    setenv("WISE_RFID_SERIAL_PORT", "/dev/ttyS9", 1);
    setenv("WISE_RFID_LEGACY_FRAMES", "0", 1);

    config_load(NULL);
    const wd_config_t *cfg = config_get();
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_EQUAL(0x12, cfg->rfid_address);
    TEST_ASSERT_EQUAL(30, cfg->rfid_power);
    TEST_ASSERT_EQUAL(115200, cfg->rfid_baudrate);
    TEST_ASSERT_EQUAL_STRING("/dev/ttyS9", cfg->rfid_serial_port);
    TEST_ASSERT_FALSE(cfg->rfid_legacy_frames);

    /* 越界/非法值必须被忽略（保留上一次的合法值），不得静默写入错误配置 */
    setenv("WISE_RFID_POWER", "99", 1);
    setenv("WISE_RFID_ADDRESS", "4096", 1);
    setenv("WISE_RFID_BAUDRATE", "-1", 1);
    config_load(NULL);
    TEST_ASSERT_EQUAL(30, cfg->rfid_power);
    TEST_ASSERT_EQUAL(0x12, cfg->rfid_address);
    TEST_ASSERT_EQUAL(115200, cfg->rfid_baudrate);

    unsetenv("WISE_RFID_ADDRESS");
    unsetenv("WISE_RFID_POWER");
    unsetenv("WISE_RFID_BAUDRATE");
    unsetenv("WISE_RFID_SERIAL_PORT");
    unsetenv("WISE_RFID_LEGACY_FRAMES");
}


/** P4-18：波特率编码表必须与参考实现一致（9600→0x00 … 57600→0x05、115200→0x06） */
void test_rfid_baud_code_matches_reference(void) {
    TEST_ASSERT_EQUAL(0x00, rfid_baud_code(9600));
    TEST_ASSERT_EQUAL(0x01, rfid_baud_code(19200));
    TEST_ASSERT_EQUAL(0x02, rfid_baud_code(38400));
    TEST_ASSERT_EQUAL(0x05, rfid_baud_code(57600));
    TEST_ASSERT_EQUAL(0x06, rfid_baud_code(115200));
    /* 不支持的速率返回 -1（参考实现里会抛异常，这里退化为错误码） */
    TEST_ASSERT_EQUAL(-1, rfid_baud_code(4800));
    TEST_ASSERT_EQUAL(-1, rfid_baud_code(0));
}

/** P4-18：地址/波特率下发在**参数非法**时必须提前拒绝（不发生串口通信） */
void test_rfid_set_config_rejects_bad_params(void) {
    /* 广播地址不能作为目标地址；非法波特率在映射阶段就被拒 */
    TEST_ASSERT_EQUAL(WD_ERR_PARAM, rfid_set_address(0xFF));
    TEST_ASSERT_EQUAL(WD_ERR_PARAM, rfid_set_baudrate(4800));

    /* 合法参数但串口未打开：应返回"状态不满足"，而不是崩溃或假成功 */
    rfid_close();
    TEST_ASSERT_EQUAL(WD_ERR_STATE, rfid_set_address(0x01));
    TEST_ASSERT_EQUAL(WD_ERR_STATE, rfid_set_baudrate(57600));
}
