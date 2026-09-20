/**
 * MQTT 客户端实现（P4-05 接收通道修复：改用 MQTTAsync）
 *
 * 背景：原实现只做 create/setCallbacks/connect/subscribe，**从未泵 MQTTClient_receive**，
 * 订阅回调永远不会被触发——设备端实际收不到任何下发任务，只能靠 HTTP 轮询兜底。
 *
 * 为什么选 MQTTAsync 而不是在同步客户端里加 receive 循环（实测记录）：
 * 本机 paho-c 1.3.14 的 `MQTTClient_receive` 在该环境下**恒返回 -1**（协议 trace 显示
 * CONNECT/CONNACK、SUBSCRIBE/SUBACK 全部成功，但 receive 立即失败），无法作为可靠接收泵；
 * 因此按任务口径采用第二种方案：MQTTAsync（库自带后台接收线程 + 自动重连）。
 *
 * 本实现要点：
 * 1. `automaticReconnect` 交给库做指数退避重连（min 1s / max 30s），连接成功回调里**重放订阅**；
 * 2. connected / 回调指针 / 订阅记忆全部由 state_mutex 保护（原先是裸 bool，回调线程与主线程并发读写）；
 * 3. 用户回调在锁外调用；回调线程内只做"发起订阅"不做等待，避免自锁；
 * 4. init/subscribe/publish 保持**同步返回语义**（信号量等回调），不改变上层调用方式。
 */

#include "infrastructure/mqtt_client.h"
#include "common/logger.h"
#include <MQTTAsync.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CONNECT_TIMEOUT_MS  10000
#define OP_TIMEOUT_MS       10000
#define DISCONNECT_TIMEOUT_MS 3000
#define MAX_REMEMBERED_SUBS 8
#define COPY_LEN            256

static MQTTAsync client = NULL;
static MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
static MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;

/* 连接参数副本：重连由库内部完成，这里只保存用于日志与初始连接 */
static char g_url[COPY_LEN];
static char g_client_id[COPY_LEN];
static char g_username[COPY_LEN];
static char g_password[COPY_LEN];

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool connected = false;                                                  /* 由 state_mutex 保护 */
static void (*message_callback)(const char *topic, const char *payload) = NULL; /* 由 state_mutex 保护 */

/* 订阅记忆：连接成功（首次或重连）后自动重放 */
static char *sub_topics[MAX_REMEMBERED_SUBS];
static int sub_qos[MAX_REMEMBERED_SUBS];
static int sub_count = 0; /* 由 state_mutex 保护 */

/* 同步等待异步操作完成的信号量（一次只允许一个操作在等） */
static sem_t g_op_sem;
static bool g_op_sem_ready = false;
static pthread_mutex_t op_mutex = PTHREAD_MUTEX_INITIALIZER;

static void op_begin(void) {
    /* 消费可能残留的信号（超时后到达的回调） */
    while (sem_trywait(&g_op_sem) == 0) {
        /* drain */
    }
}

static int op_wait(int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    if (sem_timedwait(&g_op_sem, &ts) != 0) {
        return -1; /* 超时（回调未在期限内到达） */
    }
    return 0;
}

static bool is_connected_flag(void) {
    pthread_mutex_lock(&state_mutex);
    bool v = connected;
    pthread_mutex_unlock(&state_mutex);
    return v;
}

/* ---------------- paho 回调 ---------------- */

static int message_arrived_cb(void *context, char *topicName, int topicLen, MQTTAsync_message *message) {
    (void)context;
    (void)topicLen;

    pthread_mutex_lock(&state_mutex);
    void (*cb)(const char *, const char *) = message_callback;
    pthread_mutex_unlock(&state_mutex);

    if (cb && message && message->payload) {
        char *payload = malloc((size_t)message->payloadlen + 1);
        if (payload) {
            memcpy(payload, message->payload, (size_t)message->payloadlen);
            payload[message->payloadlen] = '\0';
            cb(topicName, payload); /* 锁外调用，避免用户回调再进入本模块时自锁 */
            free(payload);
        }
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

static void connection_lost_cb(void *context, char *cause) {
    (void)context;
    pthread_mutex_lock(&state_mutex);
    connected = false;
    pthread_mutex_unlock(&state_mutex);
    LOG_WARN("MQTT connection lost: %s (automatic reconnect enabled)", cause ? cause : "(none)");
}

/* 重放订阅：paho 在重连后不会自动恢复订阅，必须由应用重订阅。
 * 注意：本函数只**发起**订阅、不等待（它会在 paho 回调线程里被调用）。 */
static void replay_subscriptions(void) {
    for (int i = 0; i < MAX_REMEMBERED_SUBS; i++) {
        pthread_mutex_lock(&state_mutex);
        const char *topic = (i < sub_count) ? sub_topics[i] : NULL;
        int qos = (i < sub_count) ? sub_qos[i] : 0;
        pthread_mutex_unlock(&state_mutex);
        if (topic == NULL) break;

        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        int rc = MQTTAsync_subscribe(client, topic, qos, &opts);
        if (rc != MQTTASYNC_SUCCESS) {
            LOG_WARN("Re-subscribe failed for %s, rc=%d", topic, rc);
        } else {
            LOG_INFO("Re-subscribed to %s", topic);
        }
    }
}

/* 连接就绪的公共处理：首次连接成功与**自动重连成功**都会走到这里 */
static void handle_connected(void) {
    pthread_mutex_lock(&state_mutex);
    connected = true;
    pthread_mutex_unlock(&state_mutex);
    LOG_INFO("Connected to MQTT broker at %s", g_url);
    replay_subscriptions();
}

/* 连接成功：首次连接的回调 */
static void on_connect_success(void *context, MQTTAsync_successData *response) {
    (void)context;
    (void)response;

    handle_connected();

    if (g_op_sem_ready) {
        sem_post(&g_op_sem);
    }
}

/* 自动重连成功的回调（由 MQTTAsync_setConnected 注册） */
static void on_reconnected(void *context, char *cause) {
    (void)context;
    LOG_INFO("MQTT automatically reconnected (cause: %s)", cause ? cause : "(none)");
    handle_connected();
}

static void on_connect_failure(void *context, MQTTAsync_failureData *response) {
    (void)context;
    LOG_ERROR("MQTT connect failed: rc=%d message=%s",
              response ? response->code : -1,
              (response && response->message) ? response->message : "(none)");
    if (g_op_sem_ready) {
        sem_post(&g_op_sem);
    }
}

static void on_op_success(void *context, MQTTAsync_successData *response) {
    (void)context;
    (void)response;
    if (g_op_sem_ready) sem_post(&g_op_sem);
}

static void on_op_failure(void *context, MQTTAsync_failureData *response) {
    (void)context;
    LOG_WARN("MQTT operation failed: rc=%d message=%s",
             response ? response->code : -1,
             (response && response->message) ? response->message : "(none)");
    if (g_op_sem_ready) sem_post(&g_op_sem);
}

static void on_disconnect_success(void *context, MQTTAsync_successData *response) {
    (void)context;
    (void)response;
    if (g_op_sem_ready) sem_post(&g_op_sem);
}

static void on_disconnect_failure(void *context, MQTTAsync_failureData *response) {
    (void)context;
    (void)response;
    if (g_op_sem_ready) sem_post(&g_op_sem);
}

/* ---------------- 订阅记忆 ---------------- */

static void remember_subscription(const char *topic, int qos) {
    pthread_mutex_lock(&state_mutex);
    for (int i = 0; i < sub_count; i++) {
        if (strcmp(sub_topics[i], topic) == 0) {
            sub_qos[i] = qos;
            pthread_mutex_unlock(&state_mutex);
            return;
        }
    }
    if (sub_count < MAX_REMEMBERED_SUBS) {
        sub_topics[sub_count] = strdup(topic);
        sub_qos[sub_count] = qos;
        sub_count++;
    } else {
        LOG_WARN("MQTT subscription table full; '%s' cannot be replayed after reconnect", topic);
    }
    pthread_mutex_unlock(&state_mutex);
}

/* ---------------- 公开 API ---------------- */

int mqtt_client_init(const char *host, int port, const char *client_id, const char *username, const char *password) {
    if (!host || host[0] == '\0') {
        LOG_ERROR("MQTT host is not configured; set the MQTT_HOST environment variable");
        return -1;
    }

    if (client != NULL) {
        LOG_WARN("mqtt_client_init called twice; closing previous client first");
        mqtt_client_cleanup();
    }

    if (!g_op_sem_ready) {
        if (sem_init(&g_op_sem, 0, 0) != 0) {
            LOG_ERROR("Failed to init MQTT operation semaphore");
            return -1;
        }
        g_op_sem_ready = true;
    }

    snprintf(g_url, sizeof(g_url), "tcp://%s:%d", host, port);
    snprintf(g_client_id, sizeof(g_client_id), "%s", client_id ? client_id : "wise-device");
    snprintf(g_username, sizeof(g_username), "%s", username ? username : "");
    snprintf(g_password, sizeof(g_password), "%s", password ? password : "");

    int rc = MQTTAsync_create(&client, g_url, g_client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTASYNC_SUCCESS) {
        LOG_ERROR("Failed to create MQTT client, return code %d", rc);
        client = NULL;
        return -1;
    }

    rc = MQTTAsync_setCallbacks(client, NULL, connection_lost_cb, message_arrived_cb, NULL);
    if (rc != MQTTASYNC_SUCCESS) {
        LOG_ERROR("Failed to set callbacks, return code %d", rc);
        MQTTAsync_destroy(&client);
        return -1;
    }

    /* 自动重连成功走的是专门的 connected 回调（不是 connect 的 onSuccess）——必须注册，
     * 否则重连后 connected 标志与订阅都不会恢复（实测：不注册时 45s 内始终视为断线）。 */
    rc = MQTTAsync_setConnected(client, NULL, on_reconnected);
    if (rc != MQTTASYNC_SUCCESS) {
        LOG_ERROR("Failed to set connected callback, return code %d", rc);
        MQTTAsync_destroy(&client);
        return -1;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    /* P4-05：断线重连交给库（指数退避 1s..30s），连接成功后由 on_connect_success 重放订阅 */
    conn_opts.automaticReconnect = 1;
    conn_opts.minRetryInterval = 1;
    conn_opts.maxRetryInterval = 30;
    conn_opts.onSuccess = on_connect_success;
    conn_opts.onFailure = on_connect_failure;
    conn_opts.context = NULL;
    if (g_username[0] != '\0') {
        conn_opts.username = g_username;
        conn_opts.password = g_password;
    }

    pthread_mutex_lock(&op_mutex);
    op_begin();
    rc = MQTTAsync_connect(client, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        pthread_mutex_unlock(&op_mutex);
        LOG_ERROR("Failed to start MQTT connect, return code %d", rc);
        MQTTAsync_destroy(&client);
        return -1;
    }
    int waited = op_wait(CONNECT_TIMEOUT_MS);
    pthread_mutex_unlock(&op_mutex);

    if (waited != 0 || !is_connected_flag()) {
        LOG_ERROR("MQTT connect did not complete within %d ms", CONNECT_TIMEOUT_MS);
        MQTTAsync_destroy(&client);
        client = NULL;
        return -1;
    }

    return 0;
}

int mqtt_client_subscribe(const char *topic, int qos) {
    if (!topic || !client) return -1;
    if (!is_connected_flag()) return -1;

    remember_subscription(topic, qos);

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = on_op_success;
    opts.onFailure = on_op_failure;
    opts.context = NULL;

    pthread_mutex_lock(&op_mutex);
    op_begin();
    int rc = MQTTAsync_subscribe(client, topic, qos, &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        pthread_mutex_unlock(&op_mutex);
        LOG_ERROR("Failed to subscribe to %s, return code %d", topic, rc);
        return -1;
    }
    int waited = op_wait(OP_TIMEOUT_MS);
    pthread_mutex_unlock(&op_mutex);

    if (waited != 0) {
        LOG_ERROR("Subscribe to %s timed out", topic);
        return -1;
    }
    LOG_INFO("Subscribed to %s", topic);
    return 0;
}

int mqtt_client_publish(const char *topic, const char *payload, int qos, int retained) {
    if (!topic || !payload || !client) return -1;
    if (!is_connected_flag()) return -1;

    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = (void *)payload;
    msg.payloadlen = (int)strlen(payload);
    msg.qos = qos;
    msg.retained = retained;

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = on_op_success;
    opts.onFailure = on_op_failure;
    opts.context = NULL;

    pthread_mutex_lock(&op_mutex);
    op_begin();
    int rc = MQTTAsync_sendMessage(client, topic, &msg, &opts);
    if (rc != MQTTASYNC_SUCCESS) {
        pthread_mutex_unlock(&op_mutex);
        LOG_ERROR("Failed to publish message, return code %d", rc);
        return -1;
    }
    int waited = op_wait(OP_TIMEOUT_MS);
    pthread_mutex_unlock(&op_mutex);

    if (waited != 0) {
        LOG_ERROR("Publish to %s timed out", topic);
        return -1;
    }
    return 0;
}

void mqtt_client_set_callback(void (*callback)(const char *topic, const char *payload)) {
    pthread_mutex_lock(&state_mutex);
    message_callback = callback;
    pthread_mutex_unlock(&state_mutex);
}

void mqtt_client_cleanup(void) {
    if (!client) {
        pthread_mutex_lock(&state_mutex);
        connected = false;
        message_callback = NULL;
        pthread_mutex_unlock(&state_mutex);
        return;
    }

    bool was_connected = is_connected_flag();

    if (was_connected && g_op_sem_ready) {
        disc_opts.onSuccess = on_disconnect_success;
        disc_opts.onFailure = on_disconnect_failure;
        disc_opts.context = NULL;

        pthread_mutex_lock(&op_mutex);
        op_begin();
        if (MQTTAsync_disconnect(client, &disc_opts) == MQTTASYNC_SUCCESS) {
            (void)op_wait(DISCONNECT_TIMEOUT_MS);
        }
        pthread_mutex_unlock(&op_mutex);
    }

    pthread_mutex_lock(&state_mutex);
    connected = false;
    message_callback = NULL;
    for (int i = 0; i < sub_count; i++) {
        free(sub_topics[i]);
        sub_topics[i] = NULL;
    }
    sub_count = 0;
    pthread_mutex_unlock(&state_mutex);

    MQTTAsync_destroy(&client);
}

bool mqtt_client_is_connected(void) {
    if (client == NULL) return false;
    if (!is_connected_flag()) return false;
    return MQTTAsync_isConnected(client) != 0;
}
