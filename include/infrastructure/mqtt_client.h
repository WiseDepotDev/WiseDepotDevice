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
int mqtt_client_init(const char *host, int port, const char *client_id, const char *username, const char *password);

/**
 * Subscribe to a topic
 * 
 * @param topic Topic to subscribe to
 * @param qos QoS level (0, 1, 2)
 * @return 0 on success, -1 on failure
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
int mqtt_client_publish(const char *topic, const char *payload, int qos, int retained);

/**
 * Set message callback
 * 
 * @param callback Function to call when message arrives
 */
void mqtt_client_set_callback(void (*callback)(const char *topic, const char *payload));

/**
 * Disconnect and cleanup
 */
void mqtt_client_cleanup(void);

/**
 * Check if connected
 * 
 * @return true if connected
 */
bool mqtt_client_is_connected(void);

#endif // WISE_DEPOT_MQTT_CLIENT_H
