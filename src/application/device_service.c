/**
 * 设备服务模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#include "application/device_service.h"
#include "application/device_internal.h"
#include "application/heartbeat_task.h"
#include "application/patrol_service.h"
#include "application/config_service.h"
#include "application/log_service.h"
#include "application/rfid_service.h"
#include "application/motor_service.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "common/sys_monitor.h"
#include "common/device_info.h"
#include "common/scheduler.h"
#include "common/backoff_strategy.h"
#include "infrastructure/http_client.h"
#include "infrastructure/server_discovery.h"
#include "infrastructure/mqtt_client.h"
#include "common/crypto.h"
#include "common/wd_error.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

/* 跨文件共享状态（P4-10 批次2b）：定义在本文件，extern 声明见 application/device_internal.h。
 * 仅可见性由 static 放宽为模块内可见，语义不变。 */
/* 全局运行标志 */
volatile int g_running = 1;
/* 重新认证标志 (0: 正常, 1: 需要重新认证) */
volatile int g_reauth_needed = 0;
/* 认证 Token */
char *g_token = NULL;
/* 刷新 Token */
char *g_refresh_token = NULL;

/* 签名密钥：统一走 config_signature_secret()（环境变量 / 配置文件注入，源码内无默认值，P4-04） */



wd_error_t device_service_init(void) {
    LOG_INFO("Device service initialized (v%s)", DEVICE_VERSION);
    if (scheduler_init() != 0) {
        LOG_ERROR("Failed to init scheduler");
        return WD_ERR_GENERAL;
    }
    
    if (config_service_init() != 0) {
        LOG_ERROR("Failed to init config service");
        return WD_ERR_GENERAL;
    }

    if (log_service_init() != 0) {
        LOG_ERROR("Failed to init log service");
        return WD_ERR_GENERAL;
    }
    
    /* P4-09：统一走 application 层的电机服务初始化（由它把配置注入 domain） */
    if (motor_service_init() != 0) {
        LOG_WARN("Failed to init motor controller, patrol tasks may not work");
    }
    
    const wd_config_t *cfg = config_get();
    patrol_service_config_t patrol_cfg = patrol_service_get_default_config();
    if (cfg) {
        patrol_cfg.task_poll_interval = cfg->task_poll_interval;
    }
    
    if (patrol_service_init(&patrol_cfg) != 0) {
        LOG_WARN("Failed to init patrol service");
    }

    // Init RFID Service
    if (cfg) {
        rfid_service_config_t rfid_cfg;
        memset(&rfid_cfg, 0, sizeof(rfid_cfg));
        if (cfg->rfid_serial_port) {
            strncpy(rfid_cfg.serial_port, cfg->rfid_serial_port, sizeof(rfid_cfg.serial_port) - 1);
        } else {
            strncpy(rfid_cfg.serial_port, "/dev/ttyUSB0", sizeof(rfid_cfg.serial_port) - 1);
        }
        rfid_cfg.baudrate = cfg->rfid_baudrate > 0 ? cfg->rfid_baudrate : 57600;
        rfid_cfg.power_dbm = cfg->rfid_power;
        rfid_cfg.address = cfg->rfid_address;
        rfid_cfg.legacy_frames = cfg->rfid_legacy_frames;
        rfid_cfg.server_url = cfg->server_url;
        
        // Topic for report
        static char report_topic[256];
        snprintf(report_topic, sizeof(report_topic), "wise/device/%s/report", cfg->device_id);
        rfid_cfg.mqtt_topic = report_topic;
        
        if (rfid_service_init(&rfid_cfg) != 0) {
            LOG_ERROR("Failed to init RFID service");
        }
    }

    // Init MQTT
    if (cfg && mqtt_client_init(cfg->mqtt_host, cfg->mqtt_port, cfg->device_id, 
                         cfg->mqtt_username, cfg->mqtt_password) == 0) {
        
        mqtt_client_set_callback(device_on_mqtt_message);
        
        char topic[256];
        snprintf(topic, sizeof(topic), "wise/device/%s/task", cfg->device_id);
        mqtt_client_subscribe(topic, 1);
        
        snprintf(topic, sizeof(topic), "wise/device/%s/config", cfg->device_id);
        mqtt_client_subscribe(topic, 1);
    } else {
        LOG_WARN("Failed to initialize MQTT client, will rely on polling");
    }
    
    return 0;
}

void device_trigger_reauth(void) {
    LOG_WARN("Re-authentication triggered");
    g_reauth_needed = 1;
}


static void heartbeat_wrapper(void *ctx) {
    (void)ctx;
    if (g_reauth_needed) {
        LOG_DEBUG("Heartbeat skipped due to pending re-auth");
        return;
    }
    heartbeat_task_execute(NULL);
}

/* P4-14：异步信号安全的关闭路径。
 * 原实现在 handler 里直接调 logger_log / scheduler_stop（可能死锁或重入，
 * clang-tidy bugprone-signal-handler 也会报）。现在 handler 只置标志，
 * 日志与 scheduler_stop 都由调度器里的 ShutdownWatcher 在**普通线程上下文**执行。 */
static volatile sig_atomic_t g_shutdown_signal = 0;

static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_shutdown_signal = (sig_atomic_t)sig;
        g_running = 0;
    }
}

static void shutdown_watcher(void *ctx) {
    (void)ctx;
    if (g_running == 0) {
        scheduler_stop();
    }
}

void handle_crash(int sig) {
    const char *msg = "[CRITICAL] Crash detected! Signal caught.\n";
    write(STDERR_FILENO, msg, strlen(msg));
    signal(sig, SIG_DFL);
    raise(sig);
}

void device_service_maintenance(void *ctx) {
    (void)ctx;
    if (g_reauth_needed) {
        // Simple backoff for re-auth could be added here
        if (device_register() == 0) {
            g_reauth_needed = 0;
        }
    }
    
    // Check MQTT connection and reconnect if needed
    const wd_config_t *cfg = config_get();
    if (cfg && !mqtt_client_is_connected()) {
        static time_t last_mqtt_retry = 0;
        time_t now = time(NULL);
        
        // Retry every 10 seconds
        if (now - last_mqtt_retry > 10) {
            LOG_WARN("MQTT disconnected, attempting to reconnect...");
            mqtt_client_cleanup();
            if (mqtt_client_init(cfg->mqtt_host, cfg->mqtt_port, cfg->device_id, 
                                 cfg->mqtt_username, cfg->mqtt_password) == 0) {
                
                mqtt_client_set_callback(device_on_mqtt_message);
                
                char topic[256];
                snprintf(topic, sizeof(topic), "wise/device/%s/task", cfg->device_id);
                mqtt_client_subscribe(topic, 1);
                
                snprintf(topic, sizeof(topic), "wise/device/%s/config", cfg->device_id);
                mqtt_client_subscribe(topic, 1);
                
                LOG_INFO("MQTT reconnected successfully");
            }
            last_mqtt_retry = now;
        }
    }
}


void device_run(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_crash;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);

    // 1. Server Discovery
    // Try to discover server for 15 seconds at startup
    if (server_discovery_start(15) != 0) {
        LOG_WARN("Server discovery timed out, using existing config");
    }

    // 2. Register
    if (device_register() != 0) {
        LOG_WARN("Initial registration failed, will retry later");
        g_reauth_needed = 1;
    }

    // 3. 首次拉取远端配置
    config_fetch_task(NULL);
    
    const wd_config_t *cfg = config_get();

    // 4. Start Tasks
    // Heartbeat (1s default)
    scheduler_add_task("Heartbeat", heartbeat_wrapper, NULL, cfg->heartbeat_interval * 1000);
    
    // 配置拉取（60s）
    scheduler_add_task("ConfigFetch", config_fetch_task, NULL, 60000);
    
    // Log Upload (5s)
    scheduler_add_task("LogUpload", log_upload_task, NULL, 5000);
    
    // System Maintenance (1s)
    scheduler_add_task("SystemMaintenance", device_service_maintenance, NULL, 1000);

    /* 关闭监视（100ms）：handler 只置 g_running=0，这里在普通上下文停调度器 */
    scheduler_add_task("ShutdownWatcher", shutdown_watcher, NULL, 100);

    LOG_INFO("Device service running...");

    // 5. Main Loop
    scheduler_run(100);
}

void device_stop(void) {
    g_running = 0;
    scheduler_stop();
    server_discovery_stop();
    config_service_stop();
    log_service_stop();
    
    if (g_token) {
        xfree(g_token);
        g_token = NULL;
    }
    
    if (g_refresh_token) {
        xfree(g_refresh_token);
        g_refresh_token = NULL;
    }
    
    LOG_INFO("Device service stopped");
}
