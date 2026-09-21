/**
 * @file motor_pwm.c
 * @brief PCA9685 PWM 低层驱动（P4-10 从 motor_controller.c 拆出，仅搬不改）。
 */

#include "domain/motor_internal.h"
#include "common/logger.h"
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* ==================== PCA9685 内部函数实现 ==================== */

/**
 * 写入 PCA9685 寄存器
 */
int pca9685_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    if (write(g_motor_ctrl.i2c_fd, buf, 2) != 2) {
        return -1;
    }
    return 0;
}

/**
 * 读取 PCA9685 寄存器
 */
int pca9685_read(uint8_t reg) {
    if (write(g_motor_ctrl.i2c_fd, &reg, 1) != 1) {
        return -1;
    }
    uint8_t value;
    if (read(g_motor_ctrl.i2c_fd, &value, 1) != 1) {
        return -1;
    }
    return value;
}

/**
 * 设置 PWM 频率
 */
int pca9685_set_pwm_freq(uint16_t freq) {
    /* 计算预分频值 */
    float prescale_val = 25000000.0f;   /* 25MHz 内部时钟 */
    prescale_val /= PWM_RESOLUTION;      /* 12位分辨率 */
    prescale_val /= freq;
    prescale_val -= 1.0f;
    
    uint8_t prescale = (uint8_t)(prescale_val + 0.5f);
    
    /* 进入睡眠模式 */
    uint8_t old_mode = pca9685_read(PCA9685_MODE1);
    uint8_t new_mode = (old_mode & 0x7F) | 0x10;
    
    if (pca9685_write(PCA9685_MODE1, new_mode) < 0) {
        return -1;
    }
    
    /* 设置预分频值 */
    if (pca9685_write(PCA9685_PRESCALE, prescale) < 0) {
        return -1;
    }
    
    /* 恢复模式 */
    if (pca9685_write(PCA9685_MODE1, old_mode) < 0) {
        return -1;
    }
    
    struct timespec ts1 = {0, 5000000}; /* 5ms */
    nanosleep(&ts1, NULL);
    
    /* 启用自动递增 */
    if (pca9685_write(PCA9685_MODE1, old_mode | 0xA1) < 0) { // 0xA1: Restart + Auto-Increment + ALLCALL
        return -1;
    }
    
    return 0;
}

/**
 * 设置 PWM 通道
 */
int pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off) {
    uint8_t reg = PCA9685_LED0_ON_L + 4 * channel;
    
    uint8_t buf[5];
    buf[0] = reg;
    buf[1] = on & 0xFF;
    buf[2] = (on >> 8) & 0xFF; // Full ON bit is bit 12 (0x1000)
    buf[3] = off & 0xFF;
    buf[4] = (off >> 8) & 0xFF; // Full OFF bit is bit 12
    
    if (write(g_motor_ctrl.i2c_fd, buf, 5) != 5) {
        return -1;
    }
    
    return 0;
}

/**
 * 设置 PWM 占空比
 */
int pca9685_set_duty_cycle(uint8_t channel, uint8_t duty_percent) {
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    
    // LOBOROBOT.py: int(pulse * (4096 / 100))
    // 50% -> 2048
    uint16_t off = (uint16_t)(duty_percent * 4095 / 100);
    return pca9685_set_pwm(channel, 0, off);
}

/**
 * 设置 PWM 通道电平
 */
int pca9685_set_level(uint8_t channel, bool level) {
    if (level) {
        // 4095 is 0xFFF. 
        // If LED_ON=0 and LED_OFF=4095, it turns on at 0 and off at 4095. 
        // Duty cycle = (4095-0)/4096 ~= 100%. Correct.
        return pca9685_set_pwm(channel, 0, 4095);
    } else {
        // LOBOROBOT.py: setLevel(0) -> setPWM(ch, 0, 0)
        return pca9685_set_pwm(channel, 0, 0);
    }
}
