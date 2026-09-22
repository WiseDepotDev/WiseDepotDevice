#include "application/rfid_service.h"
#include "infrastructure/rfid_driver.h"
#include "domain/inventory_manager.h"
#include "common/config.h"
#include "common/envelope.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "common/utils.h"
#include "common/crypto.h"
#include "infrastructure/http_client.h"
#include "infrastructure/mqtt_client.h"
#include "common/wd_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

static rfid_service_config_t service_config;
static bool is_initialized = false;
static bool is_busy = false;
static pthread_mutex_t service_mutex = PTHREAD_MUTEX_INITIALIZER;

wd_error_t rfid_service_init(const rfid_service_config_t *config) {
    if (!config) return WD_ERR_PARAM;
    
    pthread_mutex_lock(&service_mutex);
    if (is_initialized) {
        pthread_mutex_unlock(&service_mutex);
        return 0;
    }
    
    memcpy(&service_config, config, sizeof(rfid_service_config_t));
    
    // Init Driver
    rfid_config_t driver_config;
    memset(&driver_config, 0, sizeof(driver_config));
    snprintf(driver_config.serial_port, sizeof(driver_config.serial_port), "%s", config->serial_port);
    driver_config.baudrate = config->baudrate;
    driver_config.timeout_ms = 1000;
    driver_config.max_retries = 3;
    /* P4-12：读头地址/功率/旧帧开关由配置注入（domain/application 不直接读配置全局量） */
    driver_config.address = config->address;
    driver_config.power_dbm = config->power_dbm;
    driver_config.legacy_frames = config->legacy_frames;
    
    int init_rc = rfid_init(&driver_config);
    if (init_rc != 0) {
        LOG_ERROR("Failed to init RFID driver: %s", wd_error_str((wd_error_t)init_rc));
        pthread_mutex_unlock(&service_mutex);
        return (wd_error_t)init_rc;
    }
    
    // Set to Answer Mode
    if (rfid_set_mode_response() != 0) {
        LOG_ERROR("Failed to set RFID mode");
        // Continue anyway?
    }
    
    // Init Manager
    inventory_mgr_init();
    
    is_initialized = true;
    pthread_mutex_unlock(&service_mutex);
    LOG_INFO("RFID Service Initialized");
    return 0;
}

void rfid_service_cleanup(void) {
    pthread_mutex_lock(&service_mutex);
    if (is_initialized) {
        rfid_close();
        /* P4-01：预期库存数组（1000 条，约 216 KB）此前从未释放；cleanup 是它的归属点 */
        inventory_mgr_free();
        is_initialized = false;
    }
    pthread_mutex_unlock(&service_mutex);
}

bool rfid_service_is_busy(void) {
    bool busy;
    pthread_mutex_lock(&service_mutex);
    busy = is_busy;
    pthread_mutex_unlock(&service_mutex);
    return busy;
}

static int fetch_expected_inventory(void) {
    if (!service_config.server_url) return WD_ERR_STATE;
    
    char url[1024];
    
    // Generate Signature
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL)); 
    
    char nonce[32];
    if (wd_random_hex(nonce, sizeof(nonce)) == 0) {
        LOG_ERROR("无法获取随机 nonce，拉取预期库存中止");
        return WD_ERR_GENERAL;
    }
    
    const char *uri_path = "/api/inventories/all";
    
    // Build sorted query string with all params: nonce, pageSize, timestamp
    char query_string[512];
    snprintf(query_string, sizeof(query_string), "nonce=%s&pageSize=10000&timestamp=%s", nonce, timestamp);
    
    // Generate signature string: METHOD\nURI\nsortedQueryString
    char string_to_sign[2048];
    snprintf(string_to_sign, sizeof(string_to_sign), "GET\n%s\n%s", uri_path, query_string);
    
    /* 规范化待签串不含密钥，保留但降到 DEBUG 级（P4-04） */
    LOG_DEBUG("String to sign: %s", string_to_sign);
    
    unsigned char hmac_result[32];
    const char *signing_secret = config_signature_secret();
    if (signing_secret == NULL) {
        LOG_ERROR("Signature secret not configured (WISE_API_SIGNATURE_SECRET / signature_secret); expected inventory aborted");
        return WD_ERR_STATE;
    }
    hmac_sha256((const unsigned char *)signing_secret, strlen(signing_secret),
                (const unsigned char *)string_to_sign, strlen(string_to_sign), hmac_result);
    
    size_t sig_len = 0;
    char *signature = base64_encode(hmac_result, 32, &sig_len);
    
    /* P4-04：签名是可重放的认证材料，禁止写入日志（原先在 INFO 级打印完整签名） */
    LOG_DEBUG("Signature generated (len=%zu)", sig_len);
    
    char header_sign[256];
    char header_time[64];
    char header_nonce[64];
    
    snprintf(header_sign, sizeof(header_sign), "X-Signature: %s", signature);
    snprintf(header_time, sizeof(header_time), "X-Timestamp: %s", timestamp);
    snprintf(header_nonce, sizeof(header_nonce), "X-Nonce: %s", nonce);
    
    LOG_INFO("Headers: %s, %s, %s", header_sign, header_time, header_nonce);
    
    const char *headers[] = {
        "Content-Type: application/json",
        header_sign,
        header_time,
        header_nonce
    };
    
    snprintf(url, sizeof(url), "%s%s?%s", service_config.server_url, uri_path, query_string);
    http_response_t *resp = http_get(url, headers, 4);
    
    xfree(signature);
    
    if (!resp) {
        LOG_ERROR("Failed to fetch expected inventory");
        return WD_ERR_CONNECT;
    }
    
    if (resp->status_code != 200 || !resp->body) {
        LOG_ERROR("Server returned error: %d", resp->status_code);
        http_response_free(resp);
        return WD_ERR_SERVER;
    }
    
    int ret = inventory_load_expected(resp->body);
    http_response_free(resp);
    return ret;
}

static void upload_report(const inventory_report_t *report, const char *task_id) {
    char *json = inventory_report_to_json(report, task_id);
    if (!json) return;

    /* 决策 8A + 决策 3：MQTT 与 HTTP 用**同一个**信封请求体（packet_type = RFID_DATA_UPLOAD）。
     * 服务端消费侧已具备解包能力（MqttReportListener → common/protocol/EnvelopeUnwrapper），
     * 且按决策 3 已删除无信封兼容分支——信封化失败即不发（走缓存重试），不再降级发扁平报文。
     * 签名串仍是 METHOD+URI+query，与请求体无关。 */
    char request_id[WD_ENVELOPE_REQUEST_ID_CAP] = {0};
    char *enveloped = envelope_wrap_request("RFID_DATA_UPLOAD", json, request_id, sizeof(request_id));
    if (enveloped == NULL) {
        LOG_ERROR("上报报文信封化失败，改为缓存重试（决策 3：三端只支持统一信封）");
        xfree(json);
        inventory_cache_save(report);
        return;
    }
    const char *body_to_send = enveloped;

    // Try MQTT first
    if (service_config.mqtt_topic && mqtt_client_is_connected()) {
        if (mqtt_client_publish(service_config.mqtt_topic, body_to_send, 1, 0) == 0) {
            // LOG_INFO("Report uploaded via MQTT");
            free(enveloped);
            xfree(json);
            return;
        }
    }
    
    // Try HTTP if MQTT failed
    if (service_config.server_url) {
        char url[256];
        char header_request_id[192];
        snprintf(url, sizeof(url), "%s/api/inspection/report", service_config.server_url);

        const char *headers[2];
        int header_count = 1;
        headers[0] = "Content-Type: application/json";
        if (request_id[0] != '\0') {
            snprintf(header_request_id, sizeof(header_request_id), "REQUEST-ID: %s", request_id);
            headers[header_count++] = header_request_id;
        }

        http_response_t *resp = http_post(url, body_to_send, headers, header_count);
        if (resp) {
            if (resp->status_code == 200 || resp->status_code == 201) {
                // LOG_INFO("Report uploaded via HTTP");
                http_response_free(resp);
                free(enveloped);
                xfree(json);
                return;
            }
            http_response_free(resp);
        }
    }
    free(enveloped);

    // If all failed, cache it
    LOG_WARN("Upload failed, caching report");
    inventory_cache_save(report); // Might need to update cache to store task_id
    xfree(json);
}

int rfid_service_fetch_expected_inventory(void) {
    if (!is_initialized) return WD_ERR_STATE;
    return fetch_expected_inventory();
}

int rfid_service_scan_only(rfid_tag_t *tags, int max_count) {
    if (!tags || max_count <= 0) return WD_ERR_PARAM;
    pthread_mutex_lock(&service_mutex);
    if (!is_initialized) {
        pthread_mutex_unlock(&service_mutex);
        return WD_ERR_PARAM;
    }
    int count = (int)rfid_inventory(tags, (size_t)max_count);
    pthread_mutex_unlock(&service_mutex);
    return count;
}

void rfid_service_upload_inspection_report(const inventory_report_t *report, const char *task_id) {
    if (!report) return;
    upload_report(report, task_id);
}

wd_error_t rfid_service_run_cycle(const char *task_id) {
    pthread_mutex_lock(&service_mutex);
    if (!is_initialized || is_busy) {
        pthread_mutex_unlock(&service_mutex);
        return WD_ERR_STATE;
    }
    is_busy = true;
    pthread_mutex_unlock(&service_mutex);
    
    // LOG_INFO("Starting Inventory Cycle"); // Removed to prevent log spamming
    
    // 1. Scan
    #define MAX_SCAN_TAGS 1000
    // Use xcalloc to ensure zero-initialization (critical for tid_len)
    rfid_tag_t *tags = (rfid_tag_t *)xcalloc_try(MAX_SCAN_TAGS, sizeof(rfid_tag_t));
    if (!tags) {
        /* P4-06：内存不足时降级返回错误码，并把 busy 状态复位（不再 exit） */
        LOG_ERROR("分配扫描缓冲区失败（内存不足），本次盘点中止");
        pthread_mutex_lock(&service_mutex);
        is_busy = false;
        pthread_mutex_unlock(&service_mutex);
        return WD_ERR_NOMEM;
    }
    int count = rfid_inventory(tags, MAX_SCAN_TAGS);
    
    if (count < 0) {
        LOG_ERROR("Inventory Scan Failed: %s", wd_error_str((wd_error_t)count));
        xfree(tags);
        pthread_mutex_lock(&service_mutex);
        is_busy = false;
        pthread_mutex_unlock(&service_mutex);
        return (wd_error_t)count;
    }
    
    // Log only when tags are found, and only display the card numbers (EPC)
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            char epc_str[65];
            bytes_to_hex(tags[i].epc, tags[i].epc_len, epc_str, sizeof(epc_str));
            LOG_INFO("Scanned Tag EPC: %s", epc_str);
        }
    }
    
    /* 2. 拉取预期库存。策略：失败只告警并沿用"上一次加载的列表"（内存里已有内容不回滚），
     * 因此盘亏可能漏报、盘盈仍然成立；缓存回退尚未实现（见任务清单 P4-13 发现项）。 */
    if (fetch_expected_inventory() != 0) {
        // LOG_WARN("Could not fetch expected inventory, proceeding with current/empty list"); // Reduce spam
    }
    
    // 3. Compare
    inventory_report_t *report = inventory_process_scan(tags, count);
    xfree(tags); // Done with raw tags
    
    // 4. Upload
    if (count > 0 || report->total_expected > 0) {
        upload_report(report, task_id);
    }
    
    // Cleanup
    inventory_free_report(report);
    
    pthread_mutex_lock(&service_mutex);
    is_busy = false;
    pthread_mutex_unlock(&service_mutex);
    
    // LOG_INFO("Inventory Cycle Completed"); // Removed to prevent log spamming
    return 0;
}
