#ifndef WISE_DEPOT_MOTOR_SERVICE_H
#define WISE_DEPOT_MOTOR_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize motor service
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 初始化电机服务（应用层封装）
 * @return 0 成功；负值失败
 */
int motor_service_init(void);

/**
 * Cleanup motor service resources
 */
/**
 * @brief 停止电机并释放电机服务资源
 */
void motor_service_cleanup(void);

/**
 * Move forward with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 前进
 * @param speed 速度百分比（0-100）
 * @return 0 成功；负值失败
 */
int motor_move_forward(int speed);

/**
 * Move backward with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 后退
 * @param speed 速度百分比（0-100）
 * @return 0 成功；负值失败
 */
int motor_move_backward(int speed);

/**
 * Turn left with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 左转
 * @param speed 速度百分比（0-100）
 * @return 0 成功；负值失败
 */
int motor_turn_left(int speed);

/**
 * Turn right with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 右转
 * @param speed 速度百分比（0-100）
 * @return 0 成功；负值失败
 */
int motor_turn_right(int speed);

/**
 * Stop all motors immediately
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 停止全部电机（应用层入口）
 * @return 0 成功；负值失败
 */
int motor_service_stop_all(void);

/**
 * Get current motor status
 * @return 1 if moving, 0 if stopped
 */
/**
 * @brief 取电机是否处于运动状态
 * @return 1 = 运动中，0 = 静止
 */
int motor_get_status(void);

#endif // WISE_DEPOT_MOTOR_SERVICE_H
