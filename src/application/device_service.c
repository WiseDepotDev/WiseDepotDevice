/**
 * 设备服务模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#include "application/device_service.h"
#include "application/heartbeat_task.h"
#include "application/patrol_service.h"
#include "application/config_service.h"
#include "application/log_service.h"
#include "application/rfid_service.h"
#include "domain/motor_controller.h"
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
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

/* 全局运行标志 */
static volatile int g_running = 1;
/* 重新认证标志 (0: 正常, 1: 需要重新认证) */
static volatile int g_reauth_needed = 0;
/* 认证 Token */
static char *g_token = NULL;
/* 刷新 Token */
static char *g_refresh_token = NULL;

/* 版本号 */
#define DEVICE_VERSION "v1.0.0"
/* 签名密钥：统一走 config_signature_secret()（环境变量 / 配置文件注入，源码内无默认值，P4-04） */

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

/* 获取本机内网 IP 地址 */
static void get_local_ip(char *buffer, size_t size) {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        strncpy(buffer, "127.0.0.1", size);
        return;
    }

    // 默认回环
    strncpy(buffer, "127.0.0.1", size);

    // 遍历网卡，优先找非回环的 IPv4 地址
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) continue;

            // 忽略回环接口
            if (strcmp(ifa->ifa_name, "lo") != 0 && strcmp(host, "127.0.0.1") != 0) {
                size_t host_len = strlen(host);
                if (host_len >= size) host_len = size - 1;
                memcpy(buffer, host, host_len);
                buffer[host_len] = '\0';
                break; 
            }
        }
    }

    freeifaddrs(ifaddr);
}

static char *extract_json_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    char *p = strstr(json, search_key);
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    char *start = strchr(p, '"');
    if (!start) return NULL;
    start++;
    char *end = strchr(start, '"');
    if (!end) return NULL;
    size_t len = end - start;
    char *val = xmalloc(len + 1);
    strncpy(val, start, len);
    val[len] = '\0';
    return val;
}

void device_on_mqtt_message(const char *topic, const char *payload) {
    LOG_INFO("MQTT Message received on %s: %s", topic, payload);
    
    // Check for configuration update
    if (strstr(topic, "/config") != NULL) {
        LOG_INFO("Received configuration update via MQTT");
        if (config_update_from_json(payload) == 0) {
            LOG_INFO("Configuration applied successfully");
        } else {
            LOG_ERROR("Failed to apply configuration from MQTT");
        }
        return;
    }
    
    cJSON *json = cJSON_Parse(payload);
    if (!json) {
        LOG_ERROR("Failed to parse MQTT payload as JSON");
        return;
    }
    
    // Check if it's a task message
    // {"taskId":123, "taskType":1, "targetDistance":100.0, ...}
    
    cJSON *taskIdItem = cJSON_GetObjectItem(json, "taskId");
    cJSON *taskTypeItem = cJSON_GetObjectItem(json, "taskType");
    cJSON *targetDistanceItem = cJSON_GetObjectItem(json, "targetDistance");
    
    if (taskIdItem && taskTypeItem) {
        // Create PatrolTask
        PatrolTask *task = (PatrolTask *)xcalloc(1, sizeof(PatrolTask));
        
        // Convert ID to string
        if (cJSON_IsNumber(taskIdItem)) {
            snprintf(task->id, sizeof(task->id), "%d", taskIdItem->valueint);
        } else if (cJSON_IsString(taskIdItem)) {
            strncpy(task->id, taskIdItem->valuestring, sizeof(task->id) - 1);
        }
        
        task->type = (taskTypeItem->valueint == 0) ? PATROL_TASK_TYPE_PLAN : PATROL_TASK_TYPE_MANUAL;
        task->status = PATROL_TASK_STATUS_PENDING;
        
        // Set target points based on distance (Simplified logic)
        // If Manual task with distance, create one point.
        if (targetDistanceItem && cJSON_IsNumber(targetDistanceItem)) {
            float distance = (float)targetDistanceItem->valuedouble;

            // 1. Populate points (legacy/future support)
            task->points = (PatrolPoint *)xcalloc(1, sizeof(PatrolPoint));
            task->point_count = 1;
            task->points[0].target_distance = distance;
            task->points[0].action = PATROL_ACTION_MOVE_FORWARD;

            // 2. Populate actions (current executor support)
            // The executor in patrol_task.c only looks at task->actions, not points.
            // We need to convert distance to duration-based action.
            
            PatrolAction action;
            memset(&action, 0, sizeof(PatrolAction));
            action.type = PATROL_ACTION_MOVE_FORWARD;
            action.speed = 50; // Default speed (50%)
            
            // Simple calibration: Assuming 20 cm/s at 50% speed
            // Duration (ms) = (Distance (cm) / Speed (cm/s)) * 1000
            if (distance > 0) {
                // 20 cm/s is a conservative estimate
                action.duration_ms = (uint32_t)((distance / 20.0f) * 1000);
            } else {
                action.duration_ms = 0;
            }
            
            patrol_task_add_action(task, &action);
            
            // Add RFID scan action
            PatrolAction scan_action;
            memset(&scan_action, 0, sizeof(PatrolAction));
            scan_action.type = PATROL_ACTION_RFID_SCAN;
            scan_action.duration_ms = 2000; // Scan for 2 seconds
            patrol_task_add_action(task, &scan_action);
            
            // Also add a stop action at the end for safety
            PatrolAction stop_action;
            memset(&stop_action, 0, sizeof(PatrolAction));
            stop_action.type = PATROL_ACTION_STOP;
            stop_action.duration_ms = 0; // Immediate stop
            patrol_task_add_action(task, &stop_action);
            
            LOG_INFO("Created patrol actions: forward(%dms) + scan(2000ms) (dist: %.1fcm)", 
                     action.duration_ms, distance);
        } else {
            // Plan task might need more details (points array)
            // For now, assume simple manual task structure
            task->point_count = 0;
        }
        
        LOG_INFO("Starting MQTT task: %s (Type: %d)", task->id, task->type);
        if (patrol_service_start_task(task) != 0) {
            LOG_ERROR("Failed to start task from MQTT");
            patrol_task_free(task);
        }
    }
    
    cJSON_Delete(json);
}

int device_service_init(void) {
    LOG_INFO("Device service initialized (v%s)", DEVICE_VERSION);
    if (scheduler_init() != 0) {
        LOG_ERROR("Failed to init scheduler");
        return -1;
    }
    
    if (config_service_init() != 0) {
        LOG_ERROR("Failed to init config service");
        return -1;
    }

    if (log_service_init() != 0) {
        LOG_ERROR("Failed to init log service");
        return -1;
    }
    
    if (motor_controller_init(NULL) != 0) {
        LOG_WARN("Failed to init motor controller, patrol tasks may not work");
    }
    
    const Config *cfg = config_get();
    PatrolServiceConfig patrol_cfg = patrol_service_get_default_config();
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

int device_register(void) {
    const Config *cfg = config_get();
    const DeviceInfo *info = device_info_get();
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/device/register", cfg->server_url);
    
    char ip_addr[64];
    get_local_ip(ip_addr, sizeof(ip_addr));

    char body[2048];
    snprintf(body, sizeof(body), 
             "{"
             "\"deviceCode\": \"%s\", "
             "\"deviceName\": \"%s\", "
             "\"deviceType\": 2, "
             "\"ipAddress\": \"%s\", "
             "\"remark\": \"OS: %s, Kernel: %s, Ver: %s\""
             "}",
             cfg->device_id, 
             info->model,
             ip_addr,
             info->os_name, 
             info->kernel_ver,
             DEVICE_VERSION);
             
    // Generate Signature
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL)); 
    
    char nonce[32];
    // Use timestamp + rand to ensure uniqueness even if rand() collides
    snprintf(nonce, sizeof(nonce), "%s%d", timestamp, rand());
    
    char query_string[1024];
    snprintf(query_string, sizeof(query_string), "nonce=%s&timestamp=%s", nonce, timestamp);
    
    char string_to_sign[2048];
    const char *uri_path = "/api/device"; 
    snprintf(string_to_sign, sizeof(string_to_sign), "POST\n%s\n%s", uri_path, query_string);
    
    unsigned char hmac_result[32];
    const char *signing_secret = config_signature_secret();
    if (signing_secret == NULL) {
        LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); registration aborted");
        return -1;
    }
    hmac_sha256(signing_secret, strlen(signing_secret), string_to_sign, strlen(string_to_sign), hmac_result);
    
    size_t sig_len = 0;
    char *signature = base64_encode(hmac_result, 32, &sig_len);
    
    char header_sign[256];
    char header_time[64];
    char header_nonce[64];
    
    snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
    snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
    snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
    
    const char *headers[3];
    headers[0] = header_sign;
    headers[1] = header_time;
    headers[2] = header_nonce;
    
    snprintf(url, sizeof(url), "%s%s", cfg->server_url, uri_path);
    
    LOG_INFO("Registering device: %s (Model: %s, OS: %s)", cfg->device_id, info->model, info->os_name);
    
    HttpResponse *res = http_post_with_retry(url, body, headers, 3, 10000, 3);
    
    xfree(signature);
    
    if (!res) {
        LOG_ERROR("Registration request failed (Network error)");
        return -1;
    }
    
    if (res->status_code >= 200 && res->status_code < 300) {
        LOG_INFO("Registration successful (Status: %d)", res->status_code);
        
        char *token = extract_json_string(res->body, "token");
        char *refresh_token = extract_json_string(res->body, "refreshToken");

        if (token) {
            if (g_token) xfree(g_token);
            g_token = token;
            LOG_INFO("Received auth token: %s...", "******"); 
            
            heartbeat_set_token(g_token);
            patrol_service_set_token(g_token);
        } else {
            LOG_WARN("No token found in registration response");
        }
        
        if (refresh_token) {
            if (g_refresh_token) xfree(g_refresh_token);
            g_refresh_token = refresh_token;
            LOG_INFO("Received refresh token");
        } else {
            LOG_WARN("No refresh token found in registration response");
        }
        
        g_reauth_needed = 0;
        
        http_response_free(res);
        return 0;
    } else {
        LOG_ERROR("Registration failed (Status: %d, Body: %s)", res->status_code, res->body ? res->body : "");
        http_response_free(res);
        return -1;
    }
}

static void heartbeat_wrapper(void *ctx) {
    (void)ctx;
    if (g_reauth_needed) {
        LOG_DEBUG("Heartbeat skipped due to pending re-auth");
        return;
    }
    heartbeat_task_execute(NULL);
}

static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        LOG_INFO("Received signal %d, stopping...", sig);
        g_running = 0;
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
    const Config *cfg = config_get();
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

char *device_service_get_token(void) {
    return g_token;
}

char *device_service_get_refresh_token(void) {
    return g_refresh_token;
}

void device_service_set_tokens(const char *access_token, const char *refresh_token) {
    if (access_token) {
        if (g_token) xfree(g_token);
        g_token = xstrdup(access_token);
        heartbeat_set_token(g_token);
        patrol_service_set_token(g_token);
    }
    
    if (refresh_token) {
        if (g_refresh_token) xfree(g_refresh_token);
        g_refresh_token = xstrdup(refresh_token);
    }
}

int device_refresh_token(void) {
    const Config *cfg = config_get();
    
    if (!g_refresh_token) {
        LOG_ERROR("No refresh token available");
        return -1;
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/auth/refresh-token", cfg->server_url);
    
    char body[1024];
    snprintf(body, sizeof(body), "{\"refreshToken\": \"%s\"}", g_refresh_token);
    
    LOG_INFO("Refreshing token...");
    
    HttpResponse *res = http_post(url, body, NULL, 0);
    
    if (!res) {
        LOG_ERROR("Token refresh request failed (Network error)");
        return -1;
    }
    
    if (res->status_code >= 200 && res->status_code < 300) {
        LOG_INFO("Token refresh successful (Status: %d)", res->status_code);
        
        char *new_token = extract_json_string(res->body, "token");
        char *new_refresh_token = extract_json_string(res->body, "refreshToken");
        
        if (new_token) {
            if (g_token) xfree(g_token);
            g_token = new_token;
            heartbeat_set_token(g_token);
            patrol_service_set_token(g_token);
            LOG_INFO("New access token received");
        }
        
        if (new_refresh_token) {
            if (g_refresh_token) xfree(g_refresh_token);
            g_refresh_token = new_refresh_token;
            LOG_INFO("New refresh token received");
        }
        
        http_response_free(res);
        return 0;
    } else {
        LOG_ERROR("Token refresh failed (Status: %d, Body: %s)", res->status_code, res->body ? res->body : "");
        http_response_free(res);
        return -1;
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

    // 3. Fetch Initial Config
    config_fetch_task(NULL);
    
    const Config *cfg = config_get();

    // 4. Start Tasks
    // Heartbeat (1s default)
    scheduler_add_task("Heartbeat", heartbeat_wrapper, NULL, cfg->heartbeat_interval * 1000);
    
    // Config Fetch (60s)
    scheduler_add_task("ConfigFetch", config_fetch_task, NULL, 60000);
    
    // Log Upload (5s)
    scheduler_add_task("LogUpload", log_upload_task, NULL, 5000);
    
    // System Maintenance (1s)
    scheduler_add_task("SystemMaintenance", device_service_maintenance, NULL, 1000);

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
