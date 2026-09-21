#ifndef WISE_DEPOT_MQTT_CLIENT_H
#define WISE_DEPOT_MQTT_CLIENT_H

#include <stdbool.h>

/**
 * Initialize MQTT client and connect to broker
 * 
 * @param host Broker host
 * @param port Broker port
 * @param client_id MQTT Client ID
 * @param username Username (optional)
 * @param password Password (optional)
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 建立 MQTT 连接（含自动重连与订阅重放）
 * @param host MQTT 主机
 * @param port MQTT 端口
 * @param client_id 客户端标识
 * @param username 用户名
 * @param password 口令
 * @return 0 成功；负值失败
 */
int mqtt_client_init(const char *host, int port, const char *client_id, const char *username, const char *password);

/**
 * Subscribe to a topic
 * 
 * @param topic Topic to subscribe to
 * @param qos QoS level (0, 1, 2)
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 订阅主题（同步等待 SUBACK）
 * @param topic MQTT 主题
 * @param qos QoS 等级
 * @return 0 成功；负值失败
 */
int mqtt_client_subscribe(const char *topic, int qos);

/**
 * Publish a message to a topic
 * 
 * @param topic Topic to publish to
 * @param payload Message payload
 * @param qos QoS level
 * @param retained Retained flag
 * @return 0 on success, -1 on failure
 */
/**
 * @brief 发布消息（同步等待 PUBACK）
 * @param topic MQTT 主题
 * @param payload 报文内容
 * @param qos QoS 等级
 * @param retained 是否保留消息
 * @return 0 成功；负值失败
 */
int mqtt_client_publish(const char *topic, const char *payload, int qos, int retained);

/**
 * Set message callback
 * 
 * @param callback Function to call when message arrives
 */
/**
 * @brief 注册下行消息回调
 * @param callback 回调函数
 */
void mqtt_client_set_callback(void (*callback)(const char *topic, const char *payload));

/**
 * Disconnect and cleanup
 */
/**
 * @brief 断开并释放 MQTT 客户端
 */
void mqtt_client_cleanup(void);

/**
 * Check if connected
 * 
 * @return true if connected
 */
/**
 * @brief 取 MQTT 当前连接状态
 * @return true = 已连接
 */
bool mqtt_client_is_connected(void);

#endif // WISE_DEPOT_MQTT_CLIENT_H
