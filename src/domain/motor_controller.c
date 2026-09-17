/**
 * 电机控制模块实现
 * 
 * 实现四轮全向移动机器人的电机控制功能
 * 基于 PCA9685 PWM 控制器和 GPIO 控制
 * 
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-12
 */

#include "domain/motor_controller.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "common/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>

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

/* 全局状态 */
static struct {
    int i2c_fd;                             /**< I2C 文件描述符 */
    uint8_t i2c_address;                    /**< PCA9685 I2C 地址 */
    MotorControllerConfig config;           /**< 控制器配置 */
    bool initialized;                       /**< 初始化标志 */
    int gpio_fds[MOTOR_COUNT * 2];          /**< GPIO 文件描述符 (每个电机2个方向引脚) */
} g_motor_ctrl = {0};

/* 前向声明内部函数 */
static int pca9685_write(uint8_t reg, uint8_t value);
static int pca9685_read(uint8_t reg);
static int pca9685_set_pwm_freq(uint16_t freq);
static int pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off);
static int pca9685_set_duty_cycle(uint8_t channel, uint8_t duty_percent);
static int pca9685_set_level(uint8_t channel, bool level);
static int gpio_export(int *pin);
static int gpio_set_direction(int pin, bool output);
static int gpio_write(int pin, bool value);
static int gpio_unexport(int pin);

/**
 * 获取默认电机控制器配置
 */
MotorControllerConfig motor_controller_get_default_config(void) {
    MotorControllerConfig config = {
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
        }
    };
    return config;
}

/**
 * 初始化电机控制器
 */
int motor_controller_init(const MotorControllerConfig *config) {
    if (g_motor_ctrl.initialized) {
        LOG_WARN("Motor controller already initialized");
        return 0;
    }
    
    if (config) {
        memcpy(&g_motor_ctrl.config, config, sizeof(MotorControllerConfig));
    } else {
        g_motor_ctrl.config = motor_controller_get_default_config();
    }
    
    g_motor_ctrl.i2c_address = g_motor_ctrl.config.i2c_address;
    
    /* 打开 I2C 设备 */
    g_motor_ctrl.i2c_fd = open(I2C_DEVICE_PATH, O_RDWR);
    if (g_motor_ctrl.i2c_fd < 0) {
        LOG_ERROR("Failed to open I2C device: %s", I2C_DEVICE_PATH);
        return -1;
    }
    
    /* 设置 I2C 从机地址 */
    if (ioctl(g_motor_ctrl.i2c_fd, I2C_SLAVE, g_motor_ctrl.i2c_address) < 0) {
        LOG_ERROR("Failed to set I2C slave address: 0x%02X", g_motor_ctrl.i2c_address);
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
        return -1;
    }
    
    /* 复位 PCA9685 */
    if (pca9685_write(PCA9685_MODE1, 0x00) < 0) {
        LOG_ERROR("Failed to reset PCA9685");
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
        return -1;
    }
    
    /* 设置频率 50Hz */
    if (pca9685_set_pwm_freq(g_motor_ctrl.config.pwm_frequency) < 0) {
        LOG_ERROR("Failed to set PWM frequency");
        close(g_motor_ctrl.i2c_fd);
        g_motor_ctrl.i2c_fd = -1;
        return -1;
    }

    /* 导出 GPIO 引脚 */
    for (int i = 0; i < MOTOR_COUNT; i++) {
        MotorConfig *motor_cfg = &g_motor_ctrl.config.motors[i];
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
 * 设置单个电机运行状态
 */
int motor_run(MotorId motor, MotorDirection direction, uint8_t speed) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    if (motor >= MOTOR_COUNT) {
        LOG_ERROR("Invalid motor ID: %d", motor);
        return -1;
    }
    
    if (speed > 100) {
        LOG_WARN("Speed %d exceeds 100, clamping to 100", speed);
        speed = 100;
    }

    /* 应用电机微调参数 */
    const Config *app_config = config_get();
    float trim = 0.0f;
    switch (motor) {
        case MOTOR_A: trim = app_config->motor_trim_a; break;
        case MOTOR_B: trim = app_config->motor_trim_b; break;
        case MOTOR_C: trim = app_config->motor_trim_c; break;
        case MOTOR_D: trim = app_config->motor_trim_d; break;
        default: break;
    }
    
    // Trim > 0: 增加速度 (例如 0.1 表示增加 10% PWM)
    // Trim < 0: 减少速度
    // 注意：这里的基准是 PWM 值，不是直接加减 speed
    
    int adjusted_speed = speed;
    if (trim != 0.0f) {
        // 如果 trim 是 0.1，speed 是 50，我们希望增加 10% 的输出能力? 
        // 还是简单地增加 10% 的占空比? 
        // 让我们简单地将 speed 视为百分比，然后应用 trim
        // 例如：trim = 0.1, speed = 50 -> actual_speed = 50 * (1 + 0.1) = 55
        //      trim = -0.1, speed = 50 -> actual_speed = 50 * (1 - 0.1) = 45
        
        adjusted_speed = (int)(speed * (1.0f + trim));
        
        if (adjusted_speed > 100) adjusted_speed = 100;
        if (adjusted_speed < 0) adjusted_speed = 0;
        
        // LOG_DEBUG("Motor %d trim applied: %.2f, speed %d -> %d", motor, trim, speed, adjusted_speed);
    }
    
    MotorConfig *cfg = &g_motor_ctrl.config.motors[motor];
    
    /* 设置方向 */
    if (cfg->use_gpio_for_dir) {
        /* 使用 GPIO 控制方向 (Motor D) */
        // LOBOROBOT.py: 
        // if (index == Dir[0]) -> Forward
        //   motorD1.off() -> GPIO Low (0)
        //   motorD2.on()  -> GPIO High (1)
        // else -> Backward
        //   motorD1.on()  -> GPIO High (1)
        //   motorD2.off() -> GPIO Low (0)
        
        bool in1_val, in2_val;
        if (direction == MOTOR_DIR_FORWARD) {
            in1_val = false; // DIN1 = Low
            in2_val = true;  // DIN2 = High
        } else {
            in1_val = true;  // DIN1 = High
            in2_val = false; // DIN2 = Low
        }
        
        // Use configured GPIO pins
        gpio_write(cfg->gpio_in1, in1_val);
        gpio_write(cfg->gpio_in2, in2_val);
    } else {
        /* 使用 PCA9685 通道控制方向 (模拟 GPIO) */
        if (direction == MOTOR_DIR_FORWARD) {
            // LOBOROBOT.py: if(index == Dir[0]) -> forward
            // setLevel(AIN1, 0); setLevel(AIN2, 1);
            pca9685_set_level(cfg->in1_channel, false);
            pca9685_set_level(cfg->in2_channel, true);
        } else {
            // LOBOROBOT.py: else -> backward
            // setLevel(AIN1, 1); setLevel(AIN2, 0);
            pca9685_set_level(cfg->in1_channel, true);
            pca9685_set_level(cfg->in2_channel, false);
        }
    }
    
    /* 设置 PWM 占空比 (速度) */
    if (pca9685_set_duty_cycle(cfg->pwm_channel, (uint8_t)adjusted_speed) < 0) {
        LOG_ERROR("Failed to set PWM for motor %d", motor);
        return -1;
    }
    
    LOG_DEBUG("Motor %d running: direction=%s, speed=%d%% (adj: %d%%)", 
              motor, direction == MOTOR_DIR_FORWARD ? "FWD" : "REV", speed, adjusted_speed);
    
    return 0;
}

/**
 * 停止单个电机
 */
int motor_stop(MotorId motor) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    if (motor >= MOTOR_COUNT) {
        LOG_ERROR("Invalid motor ID: %d", motor);
        return -1;
    }
    
    MotorConfig *cfg = &g_motor_ctrl.config.motors[motor];
    
    /* 设置 PWM 占空比为 0 */
    if (pca9685_set_duty_cycle(cfg->pwm_channel, 0) < 0) {
        LOG_ERROR("Failed to stop motor %d", motor);
        return -1;
    }
    
    LOG_DEBUG("Motor %d stopped", motor);
    return 0;
}

/**
 * 停止所有电机
 */
int motor_stop_all(void) {
    for (int i = 0; i < MOTOR_COUNT; i++) {
        if (motor_stop(i) < 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * 执行移动动作 (阻塞)
 */
int motor_move(MoveDirection direction, uint8_t speed, uint32_t duration_ms) {
    if (motor_move_async(direction, speed) < 0) {
        return -1;
    }
    
    struct timespec ts = {
        .tv_sec = (duration_ms * 1000000L) / 1000000000L,
        .tv_nsec = (duration_ms * 1000000L) % 1000000000L
    };
    nanosleep(&ts, NULL);
    motor_stop_all();
    
    return 0;
}

/**
 * 执行移动动作 (不阻塞)
 */
int motor_move_async(MoveDirection direction, uint8_t speed) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    if (speed > 100) {
        speed = 100;
    }
    
    LOG_DEBUG("Moving: direction=%d, speed=%d%%", direction, speed);
    
    switch (direction) {
        case MOVE_FORWARD:
            // Forward means all motors moving "forward" in their local frame?
            // Wait, mecanum wheels need specific patterns.
            // Assuming "Forward" is X-axis positive relative to car.
            // Let's assume standard Mecanum config.
            
            // Motor A (Left Front): Forward
            // Motor B (Right Front): Forward
            // Motor C (Left Rear): Forward
            // Motor D (Right Rear): Forward
            motor_run(MOTOR_A, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_B, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_D, MOTOR_DIR_FORWARD, speed);
            break;
            
        case MOVE_BACKWARD:
            motor_run(MOTOR_A, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_B, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_D, MOTOR_DIR_BACKWARD, speed);
            break;
            
        case MOVE_LEFT:
            motor_run(MOTOR_A, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_B, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_D, MOTOR_DIR_BACKWARD, speed);
            break;
            
        case MOVE_RIGHT:
            motor_run(MOTOR_A, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_B, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_D, MOTOR_DIR_FORWARD, speed);
            break;
            
        case MOVE_TURN_LEFT:
            motor_run(MOTOR_A, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_B, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_D, MOTOR_DIR_FORWARD, speed);
            break;
            
        case MOVE_TURN_RIGHT:
            motor_run(MOTOR_A, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_B, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_D, MOTOR_DIR_BACKWARD, speed);
            break;
            
        case MOVE_FORWARD_LEFT:
            motor_stop(MOTOR_A);
            motor_run(MOTOR_B, MOTOR_DIR_FORWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_FORWARD, speed);
            motor_stop(MOTOR_D);
            break;
            
        case MOVE_FORWARD_RIGHT:
            motor_run(MOTOR_A, MOTOR_DIR_FORWARD, speed);
            motor_stop(MOTOR_B);
            motor_stop(MOTOR_C);
            motor_run(MOTOR_D, MOTOR_DIR_FORWARD, speed);
            break;
            
        case MOVE_BACKWARD_LEFT:
            motor_run(MOTOR_A, MOTOR_DIR_BACKWARD, speed);
            motor_stop(MOTOR_B);
            motor_stop(MOTOR_C);
            motor_run(MOTOR_D, MOTOR_DIR_BACKWARD, speed);
            break;
            
        case MOVE_BACKWARD_RIGHT:
            motor_stop(MOTOR_A);
            motor_run(MOTOR_B, MOTOR_DIR_BACKWARD, speed);
            motor_run(MOTOR_C, MOTOR_DIR_BACKWARD, speed);
            motor_stop(MOTOR_D);
            break;
            
        default:
            LOG_ERROR("Unknown move direction: %d", direction);
            return -1;
    }
    
    return 0;
}

/**
 * 设置舵机角度
 */
int servo_set_angle(uint8_t channel, uint8_t angle) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    if (angle > 180) {
        angle = 180;
    }
    
    /* 计算脉冲宽度: 0.5ms - 2.5ms 对应 0-180度 */
    /* PWM周期 = 20ms (50Hz), 分辨率 4096 */
    /* 0.5ms = 4096 * 0.5 / 20 = 102.4 */
    /* 2.5ms = 4096 * 2.5 / 20 = 512 */
    uint16_t pulse = (uint16_t)(102.4 + (angle * (512.0 - 102.4) / 180.0));
    
    if (pca9685_set_pwm(channel, 0, pulse) < 0) {
        LOG_ERROR("Failed to set servo angle on channel %d", channel);
        return -1;
    }
    
    LOG_DEBUG("Servo channel %d set to %d degrees (pulse: %d)", channel, angle, pulse);
    return 0;
}

/**
 * 移动指定距离 (阻塞)
 */
int motor_move_distance(MoveDirection direction, float distance_cm) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    const Config *cfg = config_get();
    float speed_cm_s = cfg->move_speed_cm_s;
    if (speed_cm_s <= 0.0f) {
        speed_cm_s = 20.0f; // Default if not configured
    }
    
    // Calculate duration: time = distance / speed
    // Use 50% PWM duty cycle for standard movement
    uint8_t pwm_speed = 50;
    
    // Ensure duration is at least 100ms
    uint32_t duration_ms = (uint32_t)((distance_cm / speed_cm_s) * 1000.0f);
    if (duration_ms < 100) {
        duration_ms = 100;
    }
    
    LOG_INFO("Moving distance %.2f cm, estimated duration %u ms (speed %.2f cm/s)", 
             distance_cm, duration_ms, speed_cm_s);
             
    return motor_move(direction, pwm_speed, duration_ms);
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
        MotorConfig *cfg = &g_motor_ctrl.config.motors[i];
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

/* ==================== PCA9685 内部函数实现 ==================== */

/**
 * 写入 PCA9685 寄存器
 */
static int pca9685_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    if (write(g_motor_ctrl.i2c_fd, buf, 2) != 2) {
        return -1;
    }
    return 0;
}

/**
 * 读取 PCA9685 寄存器
 */
static int pca9685_read(uint8_t reg) {
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
static int pca9685_set_pwm_freq(uint16_t freq) {
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
static int pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off) {
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
static int pca9685_set_duty_cycle(uint8_t channel, uint8_t duty_percent) {
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
static int pca9685_set_level(uint8_t channel, bool level) {
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

/* ==================== GPIO 内部函数实现 ==================== */

/**
 * 导出 GPIO 引脚
 */
static int gpio_export(int *pin) {
    // 尝试导出指定的引脚
    // 如果失败且是 EINVAL，尝试加上 Pi 5 的偏移量 (571 或 569)
    
    int current_pin = *pin;
    int retry_pin = -1;
    
    // 偏移量数组
    int offsets[] = {RPI5_GPIO_OFFSET_1, RPI5_GPIO_OFFSET_2};
    
    // 第一次尝试：原始引脚
    for (int attempt = 0; attempt <= 2; attempt++) {
        if (attempt > 0) {
            // 后续尝试：尝试不同的偏移量
            // 如果原始引脚已经很大了，可能不需要偏移
            if (current_pin < 500) {
                retry_pin = current_pin + offsets[attempt-1];
                LOG_INFO("Retrying GPIO export with Pi 5 offset (%d): %d -> %d", 
                         offsets[attempt-1], current_pin, retry_pin);
                // 这里需要注意，如果是第二次重试（attempt=2），需要基于原始引脚
                // 但是我们在 attempt=1 时修改了 current_pin?
                // 让我们保持简单：总是基于原始输入 pin (保存在 *pin 中)
                current_pin = *pin + offsets[attempt-1];
            } else {
                break; 
            }
        }
        
        char pin_path[64];
        snprintf(pin_path, sizeof(pin_path), "/sys/class/gpio/gpio%d", current_pin);
        if (access(pin_path, F_OK) == 0) {
            /* Already exported */
            if (attempt > 0) {
                 *pin = current_pin; // Update the caller's pin value
            }
            return 0;
        }

        char path[64];
        snprintf(path, sizeof(path), "/sys/class/gpio/export");
        
        int fd = open(path, O_WRONLY);
        if (fd < 0) {
            LOG_ERROR("Failed to open GPIO export: %s (Try running as root?)", strerror(errno));
            return -1;
        }
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", current_pin);
        if (write(fd, buf, strlen(buf)) < 0) {
            if (errno == EBUSY) {
                /* Already exported by someone else or kernel, assume OK */
                close(fd);
                if (attempt > 0) {
                     *pin = current_pin;
                }
                return 0;
            }
            
            // Only log error on last attempt
            if (attempt == 2) {
                if (errno == EINVAL) {
                    LOG_ERROR("Invalid GPIO pin %d. Check if the pin number is correct for your board.", current_pin);
                } else {
                    LOG_ERROR("Failed to write to GPIO export (%d): %s", current_pin, strerror(errno));
                }
            }
            close(fd);
            continue; // Try next attempt
        }
        
        close(fd);
        
        // Wait for udev to create the device node
        struct timespec ts2 = {0, 100000000}; /* 100ms */
        nanosleep(&ts2, NULL);
        
        if (attempt > 0) {
             *pin = current_pin; // Update successful pin
        }
        return 0;
    }
    
    return -1;
}

/**
 * 设置 GPIO 方向
 */
static int gpio_set_direction(int pin, bool output) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open GPIO direction (%s): %s", path, strerror(errno));
        return -1;
    }
    
    const char *dir = output ? "out" : "in";
    if (write(fd, dir, strlen(dir)) < 0) {
        LOG_ERROR("Failed to write GPIO direction (%s): %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

/**
 * 写入 GPIO 值
 */
static int gpio_write(int pin, bool value) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open GPIO value (%s): %s", path, strerror(errno));
        return -1;
    }
    
    char buf[2] = {value ? '1' : '0', '\0'};
    if (write(fd, buf, 1) < 0) {
        LOG_ERROR("Failed to write GPIO value (%s): %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    
    // LOG_DEBUG("GPIO %d set to %d", pin, value);
    close(fd);
    return 0;
}

/**
 * 取消导出 GPIO 引脚
 */
static int gpio_unexport(int pin) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/unexport");
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return -1;
    }
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", pin);
    if (write(fd, buf, strlen(buf)) < 0) {
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}
