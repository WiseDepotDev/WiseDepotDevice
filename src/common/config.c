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
#include "common/wd_error.h"
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
static wd_config_t *g_config = NULL;

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

/* P4-16：跨端常量对齐的告警（定义见文件后部），前置声明以便 config_load 使用 */
static void warn_if_heartbeat_too_slow(void);

static char *trim(char *s) {
    if (!s) return NULL;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static log_level_t parse_log_level(const char *level_str) {
    if (!level_str) return DEFAULT_LOG_LEVEL;
    if (strcasecmp(level_str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(level_str, "INFO") == 0) return LOG_LEVEL_INFO;
    if (strcasecmp(level_str, "WARN") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(level_str, "ERROR") == 0) return LOG_LEVEL_ERROR;
    return DEFAULT_LOG_LEVEL;
}

static int config_init_defaults(void) {
    if (g_config) return 0;
    g_config = (wd_config_t *)xcalloc_try(1, sizeof(wd_config_t));
    if (!g_config) {
        LOG_ERROR("分配配置结构失败（内存不足）");
        return WD_ERR_NOMEM;
    }
    
    g_config->server_url = xstrdup_try(DEFAULT_SERVER_URL);
    
    // Try to get hostname from environment variables for cross-platform compatibility
    char *env_hostname = getenv("HOSTNAME");
    if (!env_hostname) env_hostname = getenv("COMPUTERNAME");
    
    if (env_hostname) {
        g_config->device_id = xstrdup_try(env_hostname);
    } else {
        // Fallback: Try to get actual hostname
        char hostbuffer[256];
        if (gethostname(hostbuffer, sizeof(hostbuffer)) == 0) {
            g_config->device_id = xstrdup_try(hostbuffer);
        } else {
            g_config->device_id = xstrdup_try("unknown-device");
        }
    }
    
    // Allow overriding via DEVICE_ID env var
    char *env_device_id = getenv("DEVICE_ID");
    if (env_device_id) {
        if (g_config->device_id) xfree(g_config->device_id);
        g_config->device_id = xstrdup_try(env_device_id);
    }
    
    g_config->heartbeat_interval = DEFAULT_HEARTBEAT_INTERVAL;
    g_config->log_path = xstrdup_try(DEFAULT_LOG_PATH);
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
    g_config->api_base_url = xstrdup_try(DEFAULT_API_BASE_URL);

    // RFID 默认配置
    g_config->rfid_serial_port = xstrdup_try("/dev/ttyUSB0");
    g_config->rfid_baudrate = 57600;
    g_config->rfid_power = 26;
    g_config->rfid_address = 0xFF;         /* P4-12：默认广播，保持既有行为 */
    g_config->rfid_legacy_frames = true;   /* P4-12：默认沿用旧帧路径 */
    g_config->task_poll_interval = DEFAULT_TASK_POLL_INTERVAL;
    g_config->network_timeout = DEFAULT_NETWORK_TIMEOUT;
    g_config->log_upload_strategy = xstrdup_try("daily");
    /* 加密密钥：仅从环境变量读取，源码内不得硬编码（STD-SEC-01）。
     * 该字段目前只被赋值与释放、尚无消费方；启用本地数据加密时必须由部署方
     * 通过 WISE_ENCRYPTION_KEY 注入（建议 0600 配置文件或 systemd 环境变量）。 */
    char *env_encryption_key = getenv("WISE_ENCRYPTION_KEY");
    g_config->encryption_key = env_encryption_key ? xstrdup_try(env_encryption_key) : NULL;
    if (!g_config->encryption_key) {
        LOG_WARN("WISE_ENCRYPTION_KEY not set; local data encryption is unavailable");
    }
    g_config->version = xstrdup_try("0.2.0");

    /* MQTT：地址 / 账号 / 口令一律只从环境变量读取，源码内不得出现真实地址与口令，
     * 也不再保留"硬编码兜底默认值"——缺失时保持 NULL，由 mqtt_client_init 明确报错。 */
    char *env_mqtt_host = getenv("MQTT_HOST");
    g_config->mqtt_host = env_mqtt_host ? xstrdup_try(env_mqtt_host) : NULL;

    char *env_mqtt_port = getenv("MQTT_PORT");
    g_config->mqtt_port = env_mqtt_port ? atoi(env_mqtt_port) : 1883; // NOLINT(cert-err34-c)：范围校验在下一行
    if (g_config->mqtt_port <= 0 || g_config->mqtt_port > 65535) {
        /* P4-14：原先 atoi 解析失败会静默变成 0，表现为"MQTT 莫名不连"；这里显式告警并回退 */
        LOG_WARN("MQTT_PORT 非法（%d），回退到 1883", g_config->mqtt_port);
        g_config->mqtt_port = 1883;
    }

    char *env_mqtt_user = getenv("MQTT_USERNAME");
    g_config->mqtt_username = env_mqtt_user ? xstrdup_try(env_mqtt_user) : NULL;

    char *env_mqtt_pass = getenv("MQTT_PASSWORD");
    g_config->mqtt_password = env_mqtt_pass ? xstrdup_try(env_mqtt_pass) : NULL;

    if (!g_config->mqtt_host) {
        LOG_WARN("MQTT_HOST not set; MQTT disabled, device will rely on HTTP polling");
    }
    if (g_config->mqtt_host && !g_config->mqtt_password) {
        LOG_WARN("MQTT_PASSWORD not set; connecting to MQTT broker without credentials");
    }

    /* P4-06：内存不足时不再 exit；统一校验必需字段并把错误码交给调用方（可回滚） */
    if (!g_config->server_url || !g_config->device_id || !g_config->log_path ||
        !g_config->api_base_url || !g_config->rfid_serial_port ||
        !g_config->log_upload_strategy || !g_config->version) {
        LOG_ERROR("初始化默认配置失败（内存不足）");
        return WD_ERR_NOMEM;
    }
    return 0;
}

/* P4-08：远端配置拉取已迁出 common 层 → application 层的 config_service_fetch_remote()。
 * 本文件（common/config.c）**不再依赖 infrastructure**，只负责本地配置的装载与生命周期。 */

wd_error_t config_load(const char *config_file) {
    if (config_init_defaults() != 0) {
        return WD_ERR_NOMEM;
    }

    /* 优先级（与 config.h 的文档一致）：默认值 < 配置文件 < 环境变量。
     * P4-04：此处把「配置文件」放在「环境变量」之前解析——此前顺序相反，
     * 文件会覆盖环境变量，与文档契约不符（现场用 systemd Environment= 注入时尤其反直觉）。 */
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
                        g_config->server_url = xstrdup_try(val);
                    } else if (strcmp(key, "device_id") == 0) {
                        if (g_config->device_id) xfree(g_config->device_id);
                        g_config->device_id = xstrdup_try(val);
                    } else if (strcmp(key, "heartbeat_interval") == 0) {
                        g_config->heartbeat_interval = atoi(val);
                    } else if (strcmp(key, "log_path") == 0) {
                        if (g_config->log_path) xfree(g_config->log_path);
                        g_config->log_path = xstrdup_try(val);
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
                    } else if (strcmp(key, "signature_secret") == 0) {
                        /* P4-04：请求签名密钥（单一来源之一；环境变量优先） */
                        if (g_config->signature_secret) xfree(g_config->signature_secret);
                        g_config->signature_secret = xstrdup_try(val);
                    } else if (strcmp(key, "rfid_serial_port") == 0) {
                        if (g_config->rfid_serial_port) xfree(g_config->rfid_serial_port);
                        g_config->rfid_serial_port = xstrdup_try(val);
                    } else if (strcmp(key, "rfid_baudrate") == 0) {
                        g_config->rfid_baudrate = atoi(val);
                    } else if (strcmp(key, "rfid_power") == 0) {
                        g_config->rfid_power = atoi(val);
                    } else if (strcmp(key, "rfid_address") == 0) {
                        g_config->rfid_address = (int)strtol(val, NULL, 0); /* 允许 0xFF 写法 */
                    } else if (strcmp(key, "rfid_legacy_frames") == 0) {
                        g_config->rfid_legacy_frames = (atoi(val) != 0);
                    }
                }
            }
            fclose(fp);
        }
    }

    /* 环境变量覆盖（最高优先级）——放在文件解析之后，保证"环境变量 > 配置文件"的文档契约成立 */
    char *env_server = getenv("WISE_SERVER_URL");
    if (env_server) {
        xfree(g_config->server_url);
        g_config->server_url = xstrdup_try(env_server);
    }

    char *env_device = getenv("WISE_DEVICE_ID");
    if (env_device) {
        xfree(g_config->device_id);
        g_config->device_id = xstrdup_try(env_device);
    }

    /* 心跳间隔同样支持环境变量覆盖（与 WISE_SERVER_URL / WISE_DEVICE_ID 一致），
     * 便于部署方在不改配置文件的情况下调整；非法值忽略并保留原值。 */
    char *env_heartbeat = getenv("WISE_DEVICE_HEARTBEAT");
    if (env_heartbeat) {
        int parsed = atoi(env_heartbeat);
        if (parsed > 0) {
            g_config->heartbeat_interval = parsed;
        } else {
            LOG_WARN("Ignoring invalid WISE_DEVICE_HEARTBEAT: %s", env_heartbeat);
        }
    }

    /* P4-04：签名密钥只从环境变量 / 配置文件读取，源码内不留任何默认值。
     * 与 MQTT 口令、加密密钥同一处理方式：缺失时保持 NULL，由签名点显式报错并中止请求。 */
    char *env_signature = getenv("WISE_API_SIGNATURE_SECRET");
    if (env_signature) {
        if (g_config->signature_secret) xfree(g_config->signature_secret);
        g_config->signature_secret = xstrdup_try(env_signature);
    }
    if (!g_config->signature_secret) {
        LOG_WARN("WISE_API_SIGNATURE_SECRET not set; signed API requests are unavailable");
    }

    /* P4-12：读头参数支持环境变量覆盖（现场调试不必改配置文件）；
     * 非法值一律"忽略并告警"，不静默写入错误配置。 */
    char *env_rfid_port = getenv("WISE_RFID_SERIAL_PORT");
    if (env_rfid_port && env_rfid_port[0] != '\0') {
        xfree(g_config->rfid_serial_port);
        g_config->rfid_serial_port = xstrdup_try(env_rfid_port);
    }

    char *env_rfid_baud = getenv("WISE_RFID_BAUDRATE");
    if (env_rfid_baud) {
        int parsed = atoi(env_rfid_baud);
        if (parsed > 0) {
            g_config->rfid_baudrate = parsed;
        } else {
            LOG_WARN("Ignoring invalid WISE_RFID_BAUDRATE: %s", env_rfid_baud);
        }
    }

    char *env_rfid_power = getenv("WISE_RFID_POWER");
    if (env_rfid_power) {
        int parsed = atoi(env_rfid_power);
        if (parsed >= 0 && parsed <= 33) {
            g_config->rfid_power = parsed;
        } else {
            LOG_WARN("Ignoring out-of-range WISE_RFID_POWER: %s (valid 0..33)", env_rfid_power);
        }
    }

    char *env_rfid_addr = getenv("WISE_RFID_ADDRESS");
    if (env_rfid_addr) {
        long parsed = strtol(env_rfid_addr, NULL, 0);
        if (parsed >= 0 && parsed <= 255) {
            g_config->rfid_address = (int)parsed;
        } else {
            LOG_WARN("Ignoring out-of-range WISE_RFID_ADDRESS: %s (valid 0..255)", env_rfid_addr);
        }
    }

    char *env_rfid_legacy = getenv("WISE_RFID_LEGACY_FRAMES");
    if (env_rfid_legacy) {
        g_config->rfid_legacy_frames = (atoi(env_rfid_legacy) != 0);
    }

    /* P4-06：文件解析与环境覆盖中的 xstrdup_try 失败会留下 NULL 字段，这里统一兜底 */
    if (!g_config->server_url || !g_config->device_id) {
        LOG_ERROR("配置加载失败：必需字段缺失（内存不足？）");
        return WD_ERR_PARAM;
    }

    warn_if_heartbeat_too_slow();

    return 0;
}

/* P4-16：服务端 DeviceApplicationService.HEARTBEAT_TIMEOUT_SECONDS = 2（超过该秒数未收到心跳即置离线）。
 * 心跳间隔必须**小于**它，否则设备状态会在"在线/离线"之间抖动（P4-15 四步联调实测：3s 心跳导致来回翻转）。
 * 这里是跨端常量对齐的显式声明，不是配置项——服务端改阈值时必须同步改这里。 */
#define WD_SERVER_HEARTBEAT_TIMEOUT_S 2

static void warn_if_heartbeat_too_slow(void) {
    if (g_config && g_config->heartbeat_interval >= WD_SERVER_HEARTBEAT_TIMEOUT_S) {
        LOG_WARN("heartbeat_interval=%ds >= 服务端离线阈值 %ds，设备状态会抖动；请调小",
                 g_config->heartbeat_interval, WD_SERVER_HEARTBEAT_TIMEOUT_S);
    }
}

const char *config_signature_secret(void) {
    return g_config ? g_config->signature_secret : NULL;
}

wd_error_t config_update_from_json(const char *json_str) {
    if (!g_config && config_init_defaults() != 0) {
        return WD_ERR_NOMEM;
    }
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERROR("Failed to parse config JSON");
        return WD_ERR_PARAM;
    }
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(root, "heartbeatInterval");
    if (cJSON_IsNumber(item)) g_config->heartbeat_interval = item->valueint;
    warn_if_heartbeat_too_slow();
    
    item = cJSON_GetObjectItem(root, "apiBaseUrl");
    if (cJSON_IsString(item)) {
        if (g_config->api_base_url) xfree(g_config->api_base_url);
        g_config->api_base_url = xstrdup_try(item->valuestring);
    }
    
    item = cJSON_GetObjectItem(root, "taskPollInterval");
    if (cJSON_IsNumber(item)) g_config->task_poll_interval = item->valueint;
    
    item = cJSON_GetObjectItem(root, "logUploadStrategy");
    if (cJSON_IsString(item)) {
        if (g_config->log_upload_strategy) xfree(g_config->log_upload_strategy);
        g_config->log_upload_strategy = xstrdup_try(item->valuestring);
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
    
    /* P4-12：读头参数可由服务端下发（与本地/环境变量同一优先级链：远端 < 本地 env） */
    item = cJSON_GetObjectItem(root, "rfidBaudrate");
    if (cJSON_IsNumber(item)) g_config->rfid_baudrate = item->valueint;

    item = cJSON_GetObjectItem(root, "rfidPower");
    if (cJSON_IsNumber(item)) g_config->rfid_power = item->valueint;

    item = cJSON_GetObjectItem(root, "rfidAddress");
    if (cJSON_IsNumber(item)) g_config->rfid_address = item->valueint;

    item = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsString(item)) {
        if (g_config->version) xfree(g_config->version);
        g_config->version = xstrdup_try(item->valuestring);
    }

    if (!g_config->api_base_url || !g_config->log_upload_strategy || !g_config->version) {
        LOG_ERROR("配置更新失败：必需字段缺失（内存不足？）");
        cJSON_Delete(root);
        return WD_ERR_PARAM;
    }
    LOG_INFO("Configuration updated to version %s", g_config->version);
    
    cJSON_Delete(root);
    
    /* 落盘非敏感快照（wise-device.dat）——便于排障与后续读回；
     * 该文件不含任何口令/令牌/密钥（M-08）。 */
    config_save_persistent();
    
    return 0;
}

/**
 * 把当前配置的**非敏感快照**写入 wise-device.dat
 *
 * M-08 实测（2026-02-27）：产物不含任何口令/令牌/密钥——敏感值只从配置文件（0600）
 * 或环境变量读取，从不写入本文件，因此无需加密；旧名 config_save_encrypted()
 * 既不真实也容易误导（源码里曾长期挂着 TODO: Implement AES encryption），已改为现名。
 */
wd_error_t config_save_persistent(void) {
    if (!g_config) return WD_ERR_STATE;
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        LOG_ERROR("创建配置 JSON 对象失败（内存不足）");
        return WD_ERR_NOMEM;
    }
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
    cJSON_AddNumberToObject(root, "rfidBaudrate", g_config->rfid_baudrate);
    cJSON_AddNumberToObject(root, "rfidPower", g_config->rfid_power);
    cJSON_AddNumberToObject(root, "rfidAddress", g_config->rfid_address);
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        LOG_ERROR("序列化配置 JSON 失败（内存不足）");
        cJSON_Delete(root);
        return WD_ERR_NOMEM;
    }
    
    FILE *fp = fopen(PERSISTENT_CONFIG_FILE, "w");
    if (fp) {
        fputs(json_str, fp);
        fclose(fp);
    } else {
        LOG_ERROR("Failed to write persistent config");
    }
    
    /* cJSON_PrintUnformatted 用 malloc 分配 → 必须用 free；
     * 用 xfree 会破坏本模块的分配计数（该块并未计入 g_allocation_count）。 */
    free(json_str);
    cJSON_Delete(root);
    return 0;
}

wd_error_t config_secure_delete(const char *file_path) {
    if (access(file_path, F_OK) != 0) return 0; // File doesn't exist
    
    struct stat st;
    if (stat(file_path, &st) != 0) return WD_ERR_IO;
    
    int fd = open(file_path, O_WRONLY);
    if (fd < 0) return WD_ERR_IO;
    
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
        return WD_ERR_IO;
    }
    
    LOG_INFO("Securely deleted file: %s", file_path);
    return 0;
}

void config_set_server_url(const char *url) {
    if (!g_config || !url) return;
    if (g_config->server_url) xfree(g_config->server_url);
    g_config->server_url = xstrdup_try(url);
    LOG_INFO("Config server URL updated to: %s", url);
}

const wd_config_t *config_get(void) {
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
        /* P4-01：以下字段同样是 xstrdup 分配，此前漏释放（ASan 报 13 字节泄漏 = /dev/ttyUSB0） */
        if (g_config->rfid_serial_port) xfree(g_config->rfid_serial_port);
        if (g_config->mqtt_host) xfree(g_config->mqtt_host);
        if (g_config->mqtt_username) xfree(g_config->mqtt_username);
        if (g_config->mqtt_password) xfree(g_config->mqtt_password);
        /* P4-04：签名密钥 */
        if (g_config->signature_secret) xfree(g_config->signature_secret);
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
