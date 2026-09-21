/**
 * @file motor_internal.h
 * @brief 电机控制内部共享定义（P4-10：随 motor_controller.c 拆分而引入）。
 *
 * 拆分为 motor_controller.c（公开 API 与生命周期）/ motor_motion.c（运动编排）/
 * motor_pwm.c（PCA9685 PWM）/ motor_gpio.c（GPIO sysfs）后，四个文件共享：
 * - 控制器运行状态 `g_motor_ctrl`（唯一定义在 motor_controller.c）；
 * - PCA9685 寄存器、PWM 分辨率、I2C 路径、Pi5 GPIO 偏移等常量；
 * - 低层函数原型（原先以 static 前向声明写在 motor_controller.c 里）。
 *
 * 注意：这是**模块内部**头文件，不对外暴露（外部只应包含 domain/motor_controller.h）。
 */

#ifndef WISE_DEPOT_MOTOR_INTERNAL_H
#define WISE_DEPOT_MOTOR_INTERNAL_H

#include "domain/motor_controller.h"
#include <stdbool.h>
#include <stdint.h>

/* ===== 常量（原 motor_controller.c 顶部宏，拆分后集中在此） ===== */
/* PCA9685 寄存器定义 */
#define PCA9685_MODE1           0x00
#define PCA9685_PRESCALE        0xFE
#define PCA9685_LED0_ON_L       0x06
#define PCA9685_LED0_ON_H       0x07
#define PCA9685_LED0_OFF_L      0x08
#define PCA9685_LED0_OFF_H      0x09

/* PWM 分辨率 */
#define PWM_RESOLUTION  4096

/* 默认 I2C 设备路径 */
#define I2C_DEVICE_PATH "/dev/i2c-1"

/* Pi 5 GPIO 偏移量 */
/* 
 * 根据 check_gpio.sh 输出：
 * pinctrl-rp1: Base=569, Count=54
 * 所以物理 GPIO 0 对应 sysfs 569
 * GPIO 25 = 569 + 25 = 594
 * 旧版本内核可能是 571
 */
#define RPI5_GPIO_OFFSET_1 569
#define RPI5_GPIO_OFFSET_2 571


/* ===== 控制器运行状态（跨文件共享；实例定义在 motor_controller.c） ===== */
typedef struct {
    int i2c_fd;                             /**< I2C 文件描述符 */
    uint8_t i2c_address;                    /**< PCA9685 I2C 地址 */
    MotorControllerConfig config;           /**< 控制器配置 */
    bool initialized;                       /**< 初始化标志 */
    int gpio_fds[MOTOR_COUNT * 2];          /**< GPIO 文件描述符 (每个电机2个方向引脚) */
} motor_ctrl_state_t;

extern motor_ctrl_state_t g_motor_ctrl;

/* ===== PCA9685 低层（motor_pwm.c） ===== */
int pca9685_write(uint8_t reg, uint8_t value);
int pca9685_read(uint8_t reg);
int pca9685_set_pwm_freq(uint16_t freq);
int pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off);
int pca9685_set_duty_cycle(uint8_t channel, uint8_t duty_percent);
int pca9685_set_level(uint8_t channel, bool level);

/* ===== GPIO sysfs（motor_gpio.c） ===== */
int gpio_export(int *pin);
int gpio_set_direction(int pin, bool output);
int gpio_write(int pin, bool value);
int gpio_unexport(int pin);

#endif // WISE_DEPOT_MOTOR_INTERNAL_H