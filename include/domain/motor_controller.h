/**
 * 电机控制模块头文件
 * 
 * 实现四轮全向移动机器人的电机控制功能
 * 基于 PCA9685 PWM 控制器和 GPIO 控制
 * 
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-12
 */

#ifndef WISE_DEPOT_MOTOR_CONTROLLER_H
#define WISE_DEPOT_MOTOR_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * 电机编号枚举
 */
typedef enum {
    MOTOR_A = 0,    /**< 电机A (左前) */
    MOTOR_B = 1,    /**< 电机B (右前) */
    MOTOR_C = 2,    /**< 电机C (左后) */
    MOTOR_D = 3,    /**< 电机D (右后) */
    MOTOR_COUNT = 4 /**< 电机总数 */
} MotorId;

/**
 * 电机方向枚举
 */
typedef enum {
    MOTOR_DIR_FORWARD = 0,  /**< 正转 */
    MOTOR_DIR_BACKWARD = 1  /**< 反转 */
} MotorDirection;

/**
 * 移动方向枚举
 */
typedef enum {
    MOVE_FORWARD,       /**< 前进 */
    MOVE_BACKWARD,      /**< 后退 */
    MOVE_LEFT,          /**< 左移 */
    MOVE_RIGHT,         /**< 右移 */
    MOVE_TURN_LEFT,     /**< 左转 */
    MOVE_TURN_RIGHT,    /**< 右转 */
    MOVE_FORWARD_LEFT,  /**< 前左斜 */
    MOVE_FORWARD_RIGHT, /**< 前右斜 */
    MOVE_BACKWARD_LEFT, /**< 后左斜 */
    MOVE_BACKWARD_RIGHT /**< 后右斜 */
} MoveDirection;

/**
 * 电机配置结构体
 */
typedef struct {
    uint8_t pwm_channel;    /**< PWM 通道号 */
    uint8_t in1_channel;    /**< 方向控制1通道 (PWM或GPIO) */
    uint8_t in2_channel;    /**< 方向控制2通道 (PWM或GPIO) */
    bool use_gpio_for_dir;  /**< 是否使用GPIO控制方向 */
    int gpio_in1;           /**< GPIO引脚1 (仅当use_gpio_for_dir为true时有效) */
    int gpio_in2;           /**< GPIO引脚2 (仅当use_gpio_for_dir为true时有效) */
} MotorConfig;

/**
 * 电机控制器配置结构体
 *
 * P4-09：速度与四路微调参数改为**由调用方（application 层）注入**，
 * domain 不再回头读 common 层的全局配置（消除 domain → common/config 的隐性依赖）。
 */
typedef struct {
    uint8_t i2c_address;    /**< PCA9685 I2C地址 */
    uint16_t pwm_frequency; /**< PWM频率 (Hz) */
    MotorConfig motors[MOTOR_COUNT]; /**< 电机配置数组 */
    float move_speed_cm_s;  /**< 标定速度 (cm/s)，0 表示用内置默认值 */
    float trim_a;           /**< 左前微调 (-1.0 ~ 1.0) */
    float trim_b;           /**< 右前微调 */
    float trim_c;           /**< 左后微调 */
    float trim_d;           /**< 右后微调 */
} MotorControllerConfig;

/**
 * 初始化电机控制器
 * 
 * @param config 控制器配置 (NULL则使用默认配置)
 * @return 0 成功，-1 失败
 */
int motor_controller_init(const MotorControllerConfig *config);

/**
 * 设置单个电机运行状态
 * 
 * @param motor 电机编号
 * @param direction 运行方向
 * @param speed 速度百分比 (0-100)
 * @return 0 成功，-1 失败
 */
int motor_run(MotorId motor, MotorDirection direction, uint8_t speed);

/**
 * 停止单个电机
 * 
 * @param motor 电机编号
 * @return 0 成功，-1 失败
 */
int motor_stop(MotorId motor);

/**
 * 停止所有电机
 * 
 * @return 0 成功，-1 失败
 */
int motor_stop_all(void);

/**
 * 执行移动动作
 * 
 * @param direction 移动方向
 * @param speed 速度百分比 (0-100)
 * @param duration_ms 持续时间 (毫秒)
 * @return 0 成功，-1 失败
 */
int motor_move(MoveDirection direction, uint8_t speed, uint32_t duration_ms);

/**
 * 执行移动动作 (不阻塞)
 * 
 * @param direction 移动方向
 * @param speed 速度百分比 (0-100)
 * @return 0 成功，-1 失败
 */
int motor_move_async(MoveDirection direction, uint8_t speed);

/**
 * 移动指定距离 (阻塞)
 * 
 * @param direction 移动方向
 * @param distance_cm 移动距离 (厘米)
 * @return 0 成功，-1 失败
 */
int motor_move_distance(MoveDirection direction, float distance_cm);

/**
 * 设置舵机角度
 * 
 * @param channel PWM通道号
 * @param angle 角度 (0-180)
 * @return 0 成功，-1 失败
 */
int servo_set_angle(uint8_t channel, uint8_t angle);

/**
 * 释放电机控制器资源
 */
void motor_controller_cleanup(void);

/**
 * 获取默认电机控制器配置
 * 
 * @return 默认配置结构体
 */
MotorControllerConfig motor_controller_get_default_config(void);

#endif // WISE_DEPOT_MOTOR_CONTROLLER_H
