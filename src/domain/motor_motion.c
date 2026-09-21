/**
 * @file motor_motion.c
 * @brief 运动编排（单/全电机运行、方向移动、舵机，P4-10 从 motor_controller.c 拆出，仅搬不改）。
 */

#include "domain/motor_internal.h"
#include "common/logger.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
/**
 * 设置单个电机运行状态
 */
int motor_run(motor_id_t motor, motor_direction_t direction, uint8_t speed) {
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

    /* 应用电机微调参数（P4-09：来自 init 时注入的配置，不再读 common 层全局配置） */
    float trim = 0.0f;
    switch (motor) {
        case MOTOR_A: trim = g_motor_ctrl.config.trim_a; break;
        case MOTOR_B: trim = g_motor_ctrl.config.trim_b; break;
        case MOTOR_C: trim = g_motor_ctrl.config.trim_c; break;
        case MOTOR_D: trim = g_motor_ctrl.config.trim_d; break;
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
    
    motor_config_t *cfg = &g_motor_ctrl.config.motors[motor];
    
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
int motor_stop(motor_id_t motor) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    if (motor >= MOTOR_COUNT) {
        LOG_ERROR("Invalid motor ID: %d", motor);
        return -1;
    }
    
    motor_config_t *cfg = &g_motor_ctrl.config.motors[motor];
    
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
int motor_move(move_direction_t direction, uint8_t speed, uint32_t duration_ms) {
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
int motor_move_async(move_direction_t direction, uint8_t speed) {
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
int motor_move_distance(move_direction_t direction, float distance_cm) {
    if (!g_motor_ctrl.initialized) {
        LOG_ERROR("Motor controller not initialized");
        return -1;
    }
    
    /* P4-09：标定速度同样来自 init 注入的配置 */
    float speed_cm_s = g_motor_ctrl.config.move_speed_cm_s;
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
