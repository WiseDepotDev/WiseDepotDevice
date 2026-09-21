/**
 * 服务端发现模块实现
 *
 * @author xingchentye
 * @version 0.1.0
 * @since 2026-03-13
 */

#include "infrastructure/server_discovery.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include "common/wd_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <cJSON.h>

#define BROADCAST_PORT 8080
#define BROADCAST_INTERVAL_SEC 3
#define RECV_TIMEOUT_SEC 1

static int sock_fd = -1;
static struct sockaddr_in broadcast_addr;

wd_error_t server_discovery_init(void) {
    /* 创建 UDP socket：先赋值再判断（clang-tidy bugprone-assignment-in-if-condition） */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return WD_ERR_IO;
    }

    // 设置广播选项
    int broadcast = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        LOG_ERROR("Failed to set SO_BROADCAST: %s", strerror(errno));
        close(sock_fd);
        return WD_ERR_IO;
    }

    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_ERROR("Failed to set SO_RCVTIMEO: %s", strerror(errno));
        close(sock_fd);
        return WD_ERR_IO;
    }

    // 设置广播地址
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    LOG_INFO("Server discovery initialized (Port: %d)", BROADCAST_PORT);
    return 0;
}

static char *create_probe_packet(void) {
    const wd_config_t *cfg = config_get();
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "type", "DISCOVERY_PROBE");
    cJSON_AddStringToObject(root, "deviceId", cfg->device_id ? cfg->device_id : "unknown");
    cJSON_AddStringToObject(root, "deviceType", "AGV_ROBOT");
    cJSON_AddStringToObject(root, "firmwareVersion", "1.0.0"); // TODO: use real version
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static int verify_and_save_server(const char *response_json) {
    cJSON *root = cJSON_Parse(response_json);
    if (!root) {
        LOG_WARN("Failed to parse discovery response: %s", response_json);
        return WD_ERR_PARAM;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "DISCOVERY_RESPONSE") != 0) {
        cJSON_Delete(root);
        return WD_ERR_PROTOCOL; // Not a discovery response
    }

    cJSON *ip = cJSON_GetObjectItem(root, "serverIp");
    cJSON *port = cJSON_GetObjectItem(root, "serverPort");
    
    if (cJSON_IsString(ip) && cJSON_IsNumber(port)) {
        char url[256];
        snprintf(url, sizeof(url), "http://%s:%d", ip->valuestring, port->valueint);
        
        LOG_INFO("Discovered server at: %s", url);
        
        // Update config with new server URL
        config_set_server_url(url);
        
        cJSON_Delete(root);
        return 0;
    }

    cJSON_Delete(root);
    return WD_ERR_PARAM;
}

wd_error_t server_discovery_start(int timeout_sec) {
    if (sock_fd < 0) {
        int init_rc = server_discovery_init();
        if (init_rc < 0) return (wd_error_t)init_rc;
    }

    time_t start_time = time(NULL);
    char buffer[4096];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    LOG_INFO("Starting server discovery...");

    while (timeout_sec == 0 || (time(NULL) - start_time < timeout_sec)) {
        char *probe = create_probe_packet();
        if (sendto(sock_fd, probe, strlen(probe), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
            LOG_WARN("Failed to send probe packet: %s", strerror(errno));
        }
        free(probe);

        int n = recvfrom(sock_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&sender_addr, &addr_len);
        if (n > 0) {
            buffer[n] = '\0';
            LOG_DEBUG("Received UDP packet from %s: %s", inet_ntoa(sender_addr.sin_addr), buffer);
            
            if (verify_and_save_server(buffer) == 0) {
                LOG_INFO("Server discovery successful!");
                return 0;
            }
        }

        if (n <= 0) {
            sleep(2); 
        }
    }

    /* 发现超时后**不再**回退到硬编码的内网地址（STD-SEC-01）。
     * 保留调用方已配置的 server_url（默认 http://localhost:8080 或 WISE_SERVER_URL）；
     * 如确需固定兜底地址，由部署方通过 WISE_FALLBACK_SERVER_URL 注入。 */
    LOG_WARN("Server discovery timed out; keeping the configured server URL "
             "(set WISE_SERVER_URL or WISE_FALLBACK_SERVER_URL to override)");

    const char *fallback = getenv("WISE_FALLBACK_SERVER_URL");
    if (fallback && fallback[0] != '\0') {
        config_set_server_url(fallback);
        LOG_INFO("Fallback server URL set from WISE_FALLBACK_SERVER_URL: %s", fallback);
        return 0;
    }

    return WD_ERR_NOT_FOUND;
}

void server_discovery_stop(void) {
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}
