/**
 * WiseDepot 设备端主程序
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-06
 */

#include "common/config.h"
#include "common/logger.h"
#include "application/config_service.h"
#include "application/motor_service.h"
#include "application/device_service.h"
#include "domain/motor_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char *argv[]) {
    // 0. 初始化随机数种子
    srand(time(NULL));

    // 1. 初始化基础日志 (默认输出到 stderr)
    logger_init(NULL, LOG_LEVEL_INFO);
    
    LOG_INFO("WiseDepot Device Client v1.0.0 Starting...");
    
    // 2. 加载配置 & 解析参数
    const char *config_file = NULL;
    bool calibrate_mode = false;
    float test_distance = 0.0f;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--calibrate") == 0) {
            calibrate_mode = true;
        } else if (strcmp(argv[i], "--test") == 0) {
            if (i + 1 < argc) {
                test_distance = (float)atof(argv[++i]);
            } else {
                LOG_ERROR("--test requires a distance argument (cm)");
                return EXIT_FAILURE;
            }
        } else if (argv[i][0] != '-') {
            config_file = argv[i];
        }
    }
    
    // Load config (本地：环境变量 + 可选配置文件；不联网)
    if (config_load(config_file) != 0) {
        LOG_WARN("Failed to load config (Env/File), using defaults");
    }

    // 远端配置拉取：与本地加载分离，失败不影响启动（P4-02 拆出；P4-08 已迁至 application 层）
    if (config_service_fetch_remote() != 0) {
        LOG_WARN("Remote config unavailable, continue with local config");
    }
    
    const Config *cfg = config_get();
    
    // 3. 重新初始化日志 (应用配置)
    // Close previous logger if needed or just re-init
    // logger_init handles re-init gracefully if we close first?
    logger_close();
    logger_init(cfg->log_path, cfg->log_level);
    
    if (calibrate_mode || test_distance > 0.0f) {
        if (calibrate_mode) {
            LOG_INFO("=== SPEED CALIBRATION MODE ===");
        } else {
            LOG_INFO("=== DISTANCE TEST MODE ===");
        }
        
        LOG_INFO("Initializing motor controller...");
        
        /* P4-09：标定/测试模式同样走 application 层的电机服务（配置由它注入 domain） */
        if (motor_service_init() != 0) {
            LOG_ERROR("Failed to initialize motor controller");
            return EXIT_FAILURE;
        }

        if (test_distance > 0.0f) {
            // Distance Test Mode
            float duration_sec = config_calculate_move_duration(test_distance);
            if (duration_sec <= 0.0f) {
                LOG_ERROR("Invalid duration calculated. Check move_speed_cm_s config.");
            } else {
                int duration_ms = (int)(duration_sec * 1000);
                LOG_INFO("Test Distance: %.2f cm", test_distance);
                LOG_INFO("Current Speed: %.2f cm/s", cfg->move_speed_cm_s);
                LOG_INFO("Calculated Duration: %d ms", duration_ms);
                LOG_INFO("Moving in 2 seconds...");
                sleep(2);
                
                if (motor_move(MOVE_FORWARD, 50, duration_ms) != 0) {
                    LOG_ERROR("Movement failed!");
                } else {
                    LOG_INFO("Movement completed. Please measure the actual distance.");
                    LOG_INFO("Error = Actual - Expected (%.2f)", test_distance);
                }
            }
        } else {
            // Calibration Mode (Fixed 5s run)
            LOG_INFO("Moving forward for 5 seconds (Speed: 50%)...");
            LOG_INFO("Please prepare to measure distance!");
            sleep(2); // Give user time
            
            if (motor_move(MOVE_FORWARD, 50, 5000) != 0) {
                LOG_ERROR("Movement failed!");
            } else {
                LOG_INFO("Movement completed (5 seconds at 50%% PWM).");
                LOG_INFO("Please measure the distance traveled in cm (e.g., 44cm).");
                LOG_INFO("Calculate speed: speed = distance_cm / 5.0 (e.g., 44/5 = 8.8 cm/s)");
                LOG_INFO("Please update 'move_speed_cm_s' in Server Device Configuration.");
                
                // Show current calculation based on config
                if (cfg->move_speed_cm_s > 0.1f) {
                    float time_1m = config_calculate_move_duration(100.0f);
                    LOG_INFO("Current Config Speed: %.2f cm/s", cfg->move_speed_cm_s);
                    LOG_INFO("Time to walk 100cm: %.2f seconds", time_1m);
                }
            }
        }
        
        motor_controller_cleanup();
        config_free();
        logger_close();
        return EXIT_SUCCESS;
    }
    
    LOG_INFO("Config loaded: Server=%s, DeviceId=%s, Heartbeat=%ds", 
             cfg->server_url, cfg->device_id, cfg->heartbeat_interval);
             
    // 4. 初始化应用服务
    if (device_service_init() != 0) {
        LOG_ERROR("Failed to initialize device service");
        return EXIT_FAILURE;
    }
    
    // 5. 启动主循环
    device_run();
    
    // 6. 清理资源
    LOG_INFO("Shutting down...");
    device_stop();
    config_free();
    logger_close();
    
    return EXIT_SUCCESS;
}
