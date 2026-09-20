/**
 * @file mqtt_integration.c
 * @brief MQTT 接收通道集成用例（P4-05）——连接真实 broker（本地 mosquitto）。
 *
 * 与单元测试的区别：本用例需要一个真实 broker（由 run_mqtt_integration.sh 启停），
 * 因此不挂在 `make check` 里，而是独立目标 `make check-mqtt`。
 *
 * 两个阶段：
 *   phase 1（默认）：连上 → 订阅 → 发布 → **必须真的收到回调**（这就是原实现缺的接收泵）；
 *   phase 2（reconnect）：phase 1 之后，脚本杀掉 broker → 断言 is_connected 变假 →
 *                        脚本重启 broker → 断言自动重连成功，且**无需调用方重新订阅**仍能收到消息
 *                        （验证重连后的订阅重放）。
 *
 * 退出码约定：0 成功；1 参数错；2 连接失败；3 订阅失败；4 发布失败；5..7 回执校验失败；
 *             8 未观察到断线；9 重连超时；10-12 重连后的收发失败。
 */

#include "common/logger.h"
#include "infrastructure/mqtt_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TOPIC "wise/test/task"
#define WAIT_STEP_MS 100

static char g_last_topic[256];
static char g_last_payload[512];
static int g_msg_count = 0;

static void on_message(const char *topic, const char *payload) {
    snprintf(g_last_topic, sizeof(g_last_topic), "%s", topic ? topic : "");
    snprintf(g_last_payload, sizeof(g_last_payload), "%s", payload ? payload : "");
    g_msg_count++;
    printf("  [callback] topic=%s payload=%s\n", g_last_topic, g_last_payload);
    fflush(stdout);
}

static int wait_for_msgs(int target, int timeout_ms) {
    int waited = 0;
    while (g_msg_count < target && waited < timeout_ms) {
        usleep(WAIT_STEP_MS * 1000);
        waited += WAIT_STEP_MS;
    }
    return (g_msg_count >= target) ? 0 : -1;
}

static int wait_for(int (*pred)(void), int timeout_ms) {
    int waited = 0;
    while (!pred() && waited < timeout_ms) {
        usleep(WAIT_STEP_MS * 1000);
        waited += WAIT_STEP_MS;
    }
    return pred() ? waited : -1;
}

static int pred_connected(void) { return mqtt_client_is_connected() ? 1 : 0; }
static int pred_disconnected(void) { return mqtt_client_is_connected() ? 0 : 1; }

int main(int argc, char **argv) {
    const char *host = getenv("MQTT_HOST");
    if (!host || host[0] == '\0') host = "127.0.0.1";
    const char *port_env = getenv("MQTT_PORT");
    int port = port_env ? atoi(port_env) : 18830;
    int reconnect_phase = (argc > 1 && strcmp(argv[1], "reconnect") == 0);

    logger_init(NULL, LOG_LEVEL_INFO);
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (mqtt_client_init(host, port, "wise-device-integration", NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: mqtt_client_init\n");
        return 2;
    }
    mqtt_client_set_callback(on_message);

    if (mqtt_client_subscribe(TOPIC, 1) != 0) {
        fprintf(stderr, "FAIL: mqtt_client_subscribe\n");
        return 3;
    }

    if (mqtt_client_publish(TOPIC, "hello-1", 1, 0) != 0) {
        fprintf(stderr, "FAIL: mqtt_client_publish\n");
        return 4;
    }
    if (wait_for_msgs(1, 5000) != 0) {
        fprintf(stderr, "FAIL: 未收到订阅消息（接收通道未工作）\n");
        return 5;
    }
    if (strcmp(g_last_payload, "hello-1") != 0) return 6;
    if (strcmp(g_last_topic, TOPIC) != 0) return 7;
    printf("PHASE1_OK payload=%s\n", g_last_payload);

    if (reconnect_phase) {
        printf("READY_FOR_BROKER_KILL\n");

        int ms = wait_for(pred_disconnected, 20000);
        if (ms < 0) {
            fprintf(stderr, "FAIL: 未观察到断线（is_connected 一直为真）\n");
            return 8;
        }
        printf("PHASE2_DISCONNECTED after %d ms\n", ms);

        ms = wait_for(pred_connected, 45000);
        if (ms < 0) {
            fprintf(stderr, "FAIL: 自动重连超时\n");
            return 9;
        }
        printf("PHASE3_RECONNECTED after %d ms\n", ms);

        /* 关键：重连后调用方**没有**重新订阅，仍必须能收到消息 */
        if (mqtt_client_publish(TOPIC, "hello-2", 1, 0) != 0) {
            fprintf(stderr, "FAIL: 重连后发布失败\n");
            return 10;
        }
        if (wait_for_msgs(2, 10000) != 0) {
            fprintf(stderr, "FAIL: 重连后未收到消息（订阅未重放）\n");
            return 11;
        }
        if (strcmp(g_last_payload, "hello-2") != 0) return 12;
        printf("PHASE4_OK payload=%s\n", g_last_payload);
    }

    mqtt_client_cleanup();
    logger_close();
    printf("MQTT_INTEGRATION_OK\n");
    return 0;
}
