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

// motor_stop_all is already defined in motor_controller.c
// We should NOT redefine it here.
// But we declared it in motor_service.h?
// Wait, if we include motor_service.h, it declares motor_stop_all.
// And motor_controller.h also probably declares it?
// Let's check if we can just wrap it or if we should rename the service one.
// The error was "multiple definition".
// motor_controller.c has implementation of motor_stop_all.
// So we should remove the implementation from here.
// But we need to expose it via motor_service.h.
// Since motor_controller.c is compiled and linked, we can just declare it in header and NOT implement it here?
// NO, the header `motor_service.h` is included by `patrol_service.c`.
// If `motor_controller.c` implements `motor_stop_all`, then `motor_service.c` should NOT implement it.
// However, `motor_service.h` declares it.
// The best way is to rename the service function to `motor_service_stop_all` to avoid conflict,
// or just remove the implementation here and let the linker find it in `motor_controller.o`.
// But `motor_controller.c` might not be using the same header.

// Let's rename the functions in motor_service.h/c to be `motor_service_*` to avoid conflicts
// and wrap the calls.

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
