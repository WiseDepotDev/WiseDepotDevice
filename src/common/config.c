/**
 * 配置管理模块实现
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#include "common/config.h"
#include "common/xmalloc.h"
#include "common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/utsname.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <cJSON.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

/* 全局配置实例 */
static Config *g_config = NULL;

/* 默认配置 */
#define DEFAULT_SERVER_URL "http://localhost:8080"
#define DEFAULT_HEARTBEAT_INTERVAL 1
#define DEFAULT_LOG_PATH "wise-device.log"
#define DEFAULT_LOG_LEVEL LOG_LEVEL_INFO
#define DEFAULT_API_BASE_URL "/api/device"
#define DEFAULT_TASK_POLL_INTERVAL 1
#define DEFAULT_NETWORK_TIMEOUT 1000
#define DEFAULT_MOVE_SPEED_CM_S 19.7f // Calibrated speed (50% PWM -> 98.5cm/5s = 19.7cm/s)
#define PERSISTENT_CONFIG_FILE "wise-device.dat"
#define ENCRYPTION_KEY "wise-depot-secret-key-2026" // Should be in secure storage

static char *trim(char *s) {
    if (!s) return NULL;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static LogLevel parse_log_level(const char *level_str) {
    if (!level_str) return DEFAULT_LOG_LEVEL;
    if (strcasecmp(level_str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(level_str, "INFO") == 0) return LOG_LEVEL_INFO;
    if (strcasecmp(level_str, "WARN") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(level_str, "ERROR") == 0) return LOG_LEVEL_ERROR;
    return DEFAULT_LOG_LEVEL;
}

static void config_init_defaults(void) {
    if (g_config) return;
    g_config = (Config *)xcalloc(1, sizeof(Config));
    
    g_config->server_url = xstrdup(DEFAULT_SERVER_URL);
    
    // Try to get hostname from environment variables for cross-platform compatibility
    char *env_hostname = getenv("HOSTNAME");
    if (!env_hostname) env_hostname = getenv("COMPUTERNAME");
    
    if (env_hostname) {
        g_config->device_id = xstrdup(env_hostname);
    } else {
        // Fallback: Try to get actual hostname
        char hostbuffer[256];
        if (gethostname(hostbuffer, sizeof(hostbuffer)) == 0) {
            g_config->device_id = xstrdup(hostbuffer);
        } else {
            g_config->device_id = xstrdup("unknown-device");
        }
    }
    
    // Allow overriding via DEVICE_ID env var
    char *env_device_id = getenv("DEVICE_ID");
    if (env_device_id) {
        if (g_config->device_id) xfree(g_config->device_id);
        g_config->device_id = xstrdup(env_device_id);
    }
    
    g_config->heartbeat_interval = DEFAULT_HEARTBEAT_INTERVAL;
    g_config->log_path = xstrdup(DEFAULT_LOG_PATH);
    g_config->log_level = DEFAULT_LOG_LEVEL;
    g_config->move_speed_cm_s = DEFAULT_MOVE_SPEED_CM_S;
    
    // // 初始化电机微调参数
    // g_config->motor_trim_a = 0.1f; // 左前
    // g_config->motor_trim_b = -0.1f; // 右前
    // g_config->motor_trim_c = 0.1f; // 左后
    // g_config->motor_trim_d = -0.1f; // 右后
    //     // 初始化电机微调参数
    g_config->motor_trim_a = 0.9f; // 左前
    g_config->motor_trim_b = -0.5f; // 右前
    g_config->motor_trim_c = 0.9f; // 左后
    g_config->motor_trim_d = -0.5f; // 右后

    //     // 初始化电机微调参数
    // g_config->motor_trim_a = 0.6f; // 左前
    // g_config->motor_trim_b = -0.295f; // 右前
    // g_config->motor_trim_c = 0.5f; // 左后
    // g_config->motor_trim_d = -0.295f; // 右后
    
    // 动态配置默认值
    g_config->api_base_url = xstrdup(DEFAULT_API_BASE_URL);

    // RFID 默认配置
    g_config->rfid_serial_port = xstrdup("/dev/ttyUSB0");
    g_config->rfid_baudrate = 57600;
    g_config->rfid_power = 26;
    g_config->task_poll_interval = DEFAULT_TASK_POLL_INTERVAL;
    g_config->network_timeout = DEFAULT_NETWORK_TIMEOUT;
    g_config->log_upload_strategy = xstrdup("daily");
    g_config->encryption_key = xstrdup(ENCRYPTION_KEY);
    g_config->version = xstrdup("0.2.0");

    // MQTT Defaults
    char *env_mqtt_host = getenv("MQTT_HOST");
    g_config->mqtt_host = env_mqtt_host ? xstrdup(env_mqtt_host) : xstrdup("10.0.0.4");
    
    char *env_mqtt_port = getenv("MQTT_PORT");
    g_config->mqtt_port = env_mqtt_port ? atoi(env_mqtt_port) : 1883;
    
    char *env_mqtt_user = getenv("MQTT_USERNAME");
    g_config->mqtt_username = env_mqtt_user ? xstrdup(env_mqtt_user) : xstrdup("root");
    
    char *env_mqtt_pass = getenv("MQTT_PASSWORD");
    g_config->mqtt_password = env_mqtt_pass ? xstrdup(env_mqtt_pass) : xstrdup("Key-1122");
}

#include "infrastructure/http_client.h"

int config_load(const char *config_file) {
    config_init_defaults();

    // 1. Load from Environment Variables (Priority)
    char *env_server = getenv("WISE_SERVER_URL");
    if (env_server) {
        xfree(g_config->server_url);
        g_config->server_url = xstrdup(env_server);
    }
    
    char *env_device = getenv("WISE_DEVICE_ID");
    if (env_device) {
        xfree(g_config->device_id);
        g_config->device_id = xstrdup(env_device);
    }

    // 2. Fetch config from server
    // We construct a URL like: SERVER_URL/api/device/config?deviceId=...&version=...
    if (g_config->server_url && g_config->device_id) {
        char url[1024];
        snprintf(url, sizeof(url), "%s%s/config?deviceId=%s&version=%s", 
                 g_config->server_url, g_config->api_base_url, g_config->device_id, g_config->version);
        
        LOG_INFO("Fetching config from: %s", url);
        HttpResponse *res = http_get(url, NULL, 0);
        if (res) {
            if (res->status_code == 200 && res->body) {
                LOG_INFO("Config fetched successfully");
                config_update_from_json(res->body);
            } else {
                LOG_WARN("Failed to fetch config, status: %d", res->status_code);
            }
            http_response_free(res);
        } else {
            LOG_WARN("Failed to connect to config server");
        }
    }

    // 3. Fallback to file if needed (Legacy support, optional)
    if (config_file && access(config_file, R_OK) == 0) {
        FILE *fp = fopen(config_file, "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                char *p = strchr(line, '#');
                if (p) *p = '\0'; 
                
                p = strchr(line, '=');
                if (p) {
                    *p = '\0';
                    char *key = trim(line);
                    char *val = trim(p + 1);
                    
                    if (strcmp(key, "server_url") == 0) {
                        if (g_config->server_url) xfree(g_config->server_url);
                        g_config->server_url = xstrdup(val);
                    } else if (strcmp(key, "device_id") == 0) {
                        if (g_config->device_id) xfree(g_config->device_id);
                        g_config->device_id = xstrdup(val);
                    } else if (strcmp(key, "heartbeat_interval") == 0) {
                        g_config->heartbeat_interval = atoi(val);
                    } else if (strcmp(key, "log_path") == 0) {
                        if (g_config->log_path) xfree(g_config->log_path);
                        g_config->log_path = xstrdup(val);
                    } else if (strcmp(key, "log_level") == 0) {
                        g_config->log_level = parse_log_level(val);
                    } else if (strcmp(key, "move_speed_cm_s") == 0) {
                        g_config->move_speed_cm_s = atof(val);
                    } else if (strcmp(key, "motor_trim_a") == 0) {
                        g_config->motor_trim_a = atof(val);
                    } else if (strcmp(key, "motor_trim_b") == 0) {
                        g_config->motor_trim_b = atof(val);
                    } else if (strcmp(key, "motor_trim_c") == 0) {
                        g_config->motor_trim_c = atof(val);
                    } else if (strcmp(key, "motor_trim_d") == 0) {
                        g_config->motor_trim_d = atof(val);
                    }
                }
            }
            fclose(fp);
        }
    }
    
    return 0;
}

int config_update_from_json(const char *json_str) {
    if (!g_config) config_init_defaults();
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERROR("Failed to parse config JSON");
        return -1;
    }
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(root, "heartbeatInterval");
    if (cJSON_IsNumber(item)) g_config->heartbeat_interval = item->valueint;
    
    item = cJSON_GetObjectItem(root, "apiBaseUrl");
    if (cJSON_IsString(item)) {
        if (g_config->api_base_url) xfree(g_config->api_base_url);
        g_config->api_base_url = xstrdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(root, "taskPollInterval");
    if (cJSON_IsNumber(item)) g_config->task_poll_interval = item->valueint;
    
    item = cJSON_GetObjectItem(root, "logUploadStrategy");
    if (cJSON_IsString(item)) {
        if (g_config->log_upload_strategy) xfree(g_config->log_upload_strategy);
        g_config->log_upload_strategy = xstrdup(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(root, "networkTimeout");
    if (cJSON_IsNumber(item)) g_config->network_timeout = item->valueint;
    
    item = cJSON_GetObjectItem(root, "move_speed_cm_s");
    if (cJSON_IsNumber(item)) g_config->move_speed_cm_s = (float)item->valuedouble;

    item = cJSON_GetObjectItem(root, "motor_trim_a");
    if (cJSON_IsNumber(item)) g_config->motor_trim_a = (float)item->valuedouble;
    
    item = cJSON_GetObjectItem(root, "motor_trim_b");
    if (cJSON_IsNumber(item)) g_config->motor_trim_b = (float)item->valuedouble;
    
    item = cJSON_GetObjectItem(root, "motor_trim_c");
    if (cJSON_IsNumber(item)) g_config->motor_trim_c = (float)item->valuedouble;
    
    item = cJSON_GetObjectItem(root, "motor_trim_d");
    if (cJSON_IsNumber(item)) g_config->motor_trim_d = (float)item->valuedouble;
    
    item = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsString(item)) {
        if (g_config->version) xfree(g_config->version);
        g_config->version = xstrdup(item->valuestring);
    }

    LOG_INFO("Configuration updated to version %s", g_config->version);
    
    cJSON_Delete(root);
    
    // 保存到本地
    config_save_encrypted();
    
    return 0;
}

int config_save_encrypted(void) {
    if (!g_config) return -1;
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server_url", g_config->server_url);
    cJSON_AddStringToObject(root, "device_id", g_config->device_id);
    cJSON_AddNumberToObject(root, "heartbeatInterval", g_config->heartbeat_interval);
    cJSON_AddStringToObject(root, "apiBaseUrl", g_config->api_base_url);
    cJSON_AddNumberToObject(root, "taskPollInterval", g_config->task_poll_interval);
    cJSON_AddStringToObject(root, "logUploadStrategy", g_config->log_upload_strategy);
    cJSON_AddNumberToObject(root, "networkTimeout", g_config->network_timeout);
    cJSON_AddStringToObject(root, "version", g_config->version);
    cJSON_AddNumberToObject(root, "move_speed_cm_s", g_config->move_speed_cm_s);
    cJSON_AddNumberToObject(root, "motor_trim_a", g_config->motor_trim_a);
    cJSON_AddNumberToObject(root, "motor_trim_b", g_config->motor_trim_b);
    cJSON_AddNumberToObject(root, "motor_trim_c", g_config->motor_trim_c);
    cJSON_AddNumberToObject(root, "motor_trim_d", g_config->motor_trim_d);
    
    char *json_str = cJSON_PrintUnformatted(root);
    
    // 简单加密模拟 (实际应使用 OpenSSL AES)
    // 这里为了演示，只做简单处理，或者直接保存 JSON
    // TODO: Implement AES encryption
    
    FILE *fp = fopen(PERSISTENT_CONFIG_FILE, "w");
    if (fp) {
        fputs(json_str, fp); // TODO: Write encrypted data
        fclose(fp);
    } else {
        LOG_ERROR("Failed to write persistent config");
    }
    
    xfree(json_str);
    cJSON_Delete(root);
    return 0;
}

int config_secure_delete(const char *file_path) {
    if (access(file_path, F_OK) != 0) return 0; // File doesn't exist
    
    struct stat st;
    if (stat(file_path, &st) != 0) return -1;
    
    int fd = open(file_path, O_WRONLY);
    if (fd < 0) return -1;
    
    // 覆盖 3 次
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    off_t len = st.st_size;
    
    for (int i = 0; i < 3; i++) {
        lseek(fd, 0, SEEK_SET);
        for (off_t w = 0; w < len; w += sizeof(buf)) {
            write(fd, buf, (len - w > (off_t)sizeof(buf)) ? sizeof(buf) : (size_t)(len - w));
        }
        fsync(fd);
    }
    
    close(fd);
    
    // 删除文件
    if (unlink(file_path) != 0) {
        LOG_ERROR("Failed to delete file: %s", file_path);
        return -1;
    }
    
    LOG_INFO("Securely deleted file: %s", file_path);
    return 0;
}

void config_set_server_url(const char *url) {
    if (!g_config || !url) return;
    if (g_config->server_url) xfree(g_config->server_url);
    g_config->server_url = xstrdup(url);
    LOG_INFO("Config server URL updated to: %s", url);
}

const Config *config_get(void) {
    if (!g_config) config_init_defaults();
    return g_config;
}

void config_free(void) {
    if (g_config) {
        if (g_config->server_url) xfree(g_config->server_url);
        if (g_config->device_id) xfree(g_config->device_id);
        if (g_config->log_path) xfree(g_config->log_path);
        if (g_config->api_base_url) xfree(g_config->api_base_url);
        if (g_config->log_upload_strategy) xfree(g_config->log_upload_strategy);
        if (g_config->encryption_key) xfree(g_config->encryption_key);
        if (g_config->version) xfree(g_config->version);
        xfree(g_config);
        g_config = NULL;
    }
}

float config_calculate_move_duration(float distance_cm) {
    if (!g_config || g_config->move_speed_cm_s <= 0.001f) {
        return 0.0f;
    }
    // Time = Distance / Speed
    // Returns duration in seconds
    return distance_cm / g_config->move_speed_cm_s;
}
