#ifndef MOTOR_SERVICE_H
#define MOTOR_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize motor service
 * @return 0 on success, -1 on failure
 */
int motor_service_init(void);

/**
 * Cleanup motor service resources
 */
void motor_service_cleanup(void);

/**
 * Move forward with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
int motor_move_forward(int speed);

/**
 * Move backward with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
int motor_move_backward(int speed);

/**
 * Turn left with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
int motor_turn_left(int speed);

/**
 * Turn right with specified speed
 * @param speed Speed percentage (0-100)
 * @return 0 on success, -1 on failure
 */
int motor_turn_right(int speed);

/**
 * Stop all motors immediately
 * @return 0 on success, -1 on failure
 */
int motor_service_stop_all(void);

/**
 * Get current motor status
 * @return 1 if moving, 0 if stopped
 */
int motor_get_status(void);

#endif // MOTOR_SERVICE_H
