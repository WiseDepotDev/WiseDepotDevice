#include "infrastructure/mqtt_client.h"
#include "common/logger.h"
#include <MQTTClient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIMEOUT     10000L

static MQTTClient client;
static MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
static bool connected = false;
static void (*message_callback)(const char *topic, const char *payload) = NULL;

static int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    (void)context;
    (void)topicLen;
    if (message_callback) {
        char *payload = malloc(message->payloadlen + 1);
        if (payload) {
            memcpy(payload, message->payload, message->payloadlen);
            payload[message->payloadlen] = '\0';
            message_callback(topicName, payload);
            free(payload);
        }
    }
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

static void connlost(void *context, char *cause) {
    (void)context;
    LOG_WARN("MQTT Connection lost: %s", cause);
    connected = false;
    // TODO: Implement reconnection logic here or in main loop
}

int mqtt_client_init(const char *host, int port, const char *client_id, const char *username, const char *password) {
    if (!host || host[0] == '\0') {
        LOG_ERROR("MQTT host is not configured; set the MQTT_HOST environment variable");
        return -1;
    }

    char url[256];
    snprintf(url, sizeof(url), "tcp://%s:%d", host, port);

    int rc = MQTTClient_create(&client, url, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        LOG_ERROR("Failed to create MQTT client, return code %d", rc);
        return -1;
    }

    rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        LOG_ERROR("Failed to set callbacks, return code %d", rc);
        return -1;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    if (username && strlen(username) > 0) {
        conn_opts.username = username;
        conn_opts.password = password;
    }
    // conn_opts.automaticReconnect = 1; // Not supported in sync client or old versions

    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        LOG_ERROR("Failed to connect to MQTT broker, return code %d", rc);
        return -1;
    }

    LOG_INFO("Connected to MQTT broker at %s", url);
    connected = true;
    return 0;
}

int mqtt_client_subscribe(const char *topic, int qos) {
    if (!connected) return -1;
    int rc = MQTTClient_subscribe(client, topic, qos);
    if (rc != MQTTCLIENT_SUCCESS) {
        LOG_ERROR("Failed to subscribe to %s, return code %d", topic, rc);
        return -1;
    }
    LOG_INFO("Subscribed to %s", topic);
    return 0;
}

int mqtt_client_publish(const char *topic, const char *payload, int qos, int retained) {
    if (!connected) return -1;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = qos;
    pubmsg.retained = retained;
    MQTTClient_deliveryToken token;
    
    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        LOG_ERROR("Failed to publish message, return code %d", rc);
        return -1;
    }
    
    rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    if (rc != MQTTCLIENT_SUCCESS) {
        LOG_ERROR("Failed to wait for completion, return code %d", rc);
        return -1;
    }
    
    return 0;
}

void mqtt_client_set_callback(void (*callback)(const char *topic, const char *payload)) {
    message_callback = callback;
}

void mqtt_client_cleanup(void) {
    if (connected) {
        MQTTClient_disconnect(client, 10000);
    }
    MQTTClient_destroy(&client);
    connected = false;
}

bool mqtt_client_is_connected(void) {
    return connected && MQTTClient_isConnected(client);
}
