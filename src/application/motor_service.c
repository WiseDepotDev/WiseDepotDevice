#include "application/motor_service.h"
#include "domain/motor_controller.h"
#include "common/config.h"
#include "common/logger.h"
#include <stddef.h>

static int is_moving = 0;

int motor_service_init(void) {
    LOG_INFO("Motor service initialized");

    /* P4-09：由 application 层把 common 层的配置注入 domain ——
     * domain 不再回头调用 config_get()（分层围栏 check-layers 会拦住回退）。 */
    motor_controller_config_t cfg = motor_controller_get_default_config();
    const wd_config_t *app_cfg = config_get();
    if (app_cfg) {
        cfg.move_speed_cm_s = app_cfg->move_speed_cm_s;
        cfg.trim_a = app_cfg->motor_trim_a;
        cfg.trim_b = app_cfg->motor_trim_b;
        cfg.trim_c = app_cfg->motor_trim_c;
        cfg.trim_d = app_cfg->motor_trim_d;
    }

    return motor_controller_init(&cfg);
}

void motor_service_cleanup(void) {
    motor_controller_cleanup();
}

int motor_move_forward(int speed) {
    LOG_INFO("Motor: Move Forward at speed %d", speed);
    is_moving = 1;
    // Map to domain controller's move function
    return motor_move_async(MOVE_FORWARD, (uint8_t)speed);
}

int motor_move_backward(int speed) {
    LOG_INFO("Motor: Move Backward at speed %d", speed);
    is_moving = 1;
    return motor_move_async(MOVE_BACKWARD, (uint8_t)speed);
}

int motor_turn_left(int speed) {
    LOG_INFO("Motor: Turn Left at speed %d", speed);
    is_moving = 1;
    return motor_move_async(MOVE_TURN_LEFT, (uint8_t)speed);
}

int motor_turn_right(int speed) {
    LOG_INFO("Motor: Turn Right at speed %d", speed);
    is_moving = 1;
    return motor_move_async(MOVE_TURN_RIGHT, (uint8_t)speed);
}

/* 应用层封装统一用 motor_service_* 前缀：domain 层已实现 motor_stop_all，
 * 若在此重名会导致"multiple definition"，因此这里只做转发。 */

int motor_service_stop_all(void) {
    if (is_moving) {
        LOG_INFO("Motor: Stopped");
        is_moving = 0;
    }
    return motor_stop_all(); // Call domain function
}

int motor_get_status(void) {
    return is_moving;
}
