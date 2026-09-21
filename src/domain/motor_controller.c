/**
 * @file motor_controller.c
 * @brief 电机控制器公开 API 与生命周期（P4-10 拆分后保留：默认配置 / init / cleanup）。
 *
 * 拆分后的分工：
 * - motor_motion.c：运动编排（motor_run / motor_stop / motor_move / 舵机 / 距离移动）
 * - motor_pwm.c   ：PCA9685 PWM 低层
 * - motor_gpio.c  ：GPIO sysfs 低层
 * 共享状态与常量见 include/domain/motor_internal.h（唯一实例定义在下方）。
 */

#include "domain/motor_controller.h"
#include "domain/motor_internal.h"
#include "common/logger.h"
#include "common/wd_error.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>

/* 控制器运行状态（唯一定义；其余文件通过 motor_internal.h 的 extern 引用） */
motor_ctrl_state_t g_motor_ctrl = {0};
/**
 * 获取默认电机控制器配置
 */
motor_controller_config_t motor_controller_get_default_config(void) {
    motor_controller_config_t config = {
        .i2c_address = 0x40,
        .pwm_frequency = 50,
        .motors = {
            /* 电机A: 左前 */
            {
                .pwm_channel = 0,
                .in1_channel = 2, 
                .in2_channel = 1, 
                .use_gpio_for_dir = false,
                .gpio_in1 = -1,
                .gpio_in2 = -1
            },
            /* 电机B: 右前 */
            {
                .pwm_channel = 5,
                .in1_channel = 4,
                .in2_channel = 3,
                .use_gpio_for_dir = false,
                .gpio_in1 = -1,
                .gpio_in2 = -1
            },
            /* 电机C: 左后 */
            {
                .pwm_channel = 6,
                .in1_channel = 7,
                .in2_channel = 8,
                .use_gpio_for_dir = false,
                .gpio_in1 = -1,
                .gpio_in2 = -1
            },
            /* 电机D: 右后 */
            {
                .pwm_channel = 11,
                .in1_channel = 0,
                .in2_channel = 0,
                .use_gpio_for_dir = true,
                .gpio_in1 = 25, // GPIO 25 (BCM)
                .gpio_in2 = 24  // GPIO 24 (BCM)
            }
        },
        /* P4-09：速度与微调默认 0 —— 由 application 层在 init 时注入真实标定值
         *（0 表示"未注入"，trim 为 0 即不微调，speed 为 0 时用内置 20cm/s 兜底） */
        .move_speed_cm_s = 0.0f,
        .trim_a = 0.0f,
        .trim_b = 0.0f,
        .trim_c = 0.0f,
        .trim_d = 0.0f
    };
    return config;
}

/**
 * 初始化电机控制器
 */
wd_error_t motor_controller_init(const motor_controller_config_t *config) {
    if (g_motor_ctrl.initialized) {
        LOG_WARN("Motor controller already initialized");
        return 0;
    }
    
    if (config) {
        memcpy(&g_motor_ctrl.config, config, sizeof(motor_controller_config_t));
    } else {
        g_motor_ctrl.config = motor_controller_get_default_config();
    }
    
    g_motor_ctrl.i2c_address = g_motor_ctrl.config.i2c_address;
    
    /* 打开 I2C 设备 */
    g_motor_ctrl.i2c_fd = open(I2C_DEVICE_PATH, O_RDWR);
    if (g_motor_ctrl.i2c_fd < 0) {
        LOG_ERROR("Failed to open I2C device: %s", I2C_DEVICE_PATH);
        return WD_ERR_IO;
    }
    
    /* 设置 I2C 从机地址 */
    if (ioctl(g_motor_ctrl.i2c_fd, I2C_SLAVE, g_motor_ctrl.i2c_address) < 0) {
        LOG_ERROR("Failed to set I2C slave address: 0x%02X", g_motor_ctrl.i2c_address);
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
        return WD_ERR_IO;
    }
    
    /* 复位 PCA9685 */
    if (pca9685_write(PCA9685_MODE1, 0x00) < 0) {
        LOG_ERROR("Failed to reset PCA9685");
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
        return WD_ERR_IO;
    }
    
    /* 设置频率 50Hz */
    if (pca9685_set_pwm_freq(g_motor_ctrl.config.pwm_frequency) < 0) {
        LOG_ERROR("Failed to set PWM frequency");
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
        return WD_ERR_IO;
    }

    /* 导出 GPIO 引脚 */
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_config_t *motor_cfg = &g_motor_ctrl.config.motors[i];
        if (motor_cfg->use_gpio_for_dir) {
            // Pass address of gpio pin variable to update it if offset is applied
            if (gpio_export(&motor_cfg->gpio_in1) < 0 || 
                gpio_export(&motor_cfg->gpio_in2) < 0) {
                LOG_WARN("Failed to export GPIO pins for motor %d", i);
            }
            if (gpio_set_direction(motor_cfg->gpio_in1, true) < 0 ||
                gpio_set_direction(motor_cfg->gpio_in2, true) < 0) {
                LOG_WARN("Failed to set GPIO direction for motor %d", i);
            }
        }
        g_motor_ctrl.gpio_fds[i * 2] = -1;
        g_motor_ctrl.gpio_fds[i * 2 + 1] = -1;
    }
    
    g_motor_ctrl.initialized = true;
    LOG_INFO("Motor controller initialized (I2C: 0x%02X, PWM: %dHz)", 
             g_motor_ctrl.i2c_address, g_motor_ctrl.config.pwm_frequency);
    
    return 0;
}

/**
 * 释放电机控制器资源
 */
void motor_controller_cleanup(void) {
    if (!g_motor_ctrl.initialized) {
        return;
    }
    
    /* 停止所有电机 */
    motor_stop_all();
    
    /* 释放 GPIO 资源 */
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motor_config_t *cfg = &g_motor_ctrl.config.motors[i];
        if (cfg->use_gpio_for_dir) {
            gpio_unexport(cfg->gpio_in1);
            gpio_unexport(cfg->gpio_in2);
        }
    }
    
    /* 关闭 I2C 设备 */
    if (g_motor_ctrl.i2c_fd >= 0) {
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
    }
    
    g_motor_ctrl.initialized = false;
    LOG_INFO("Motor controller cleanup completed");
}
