/**
 * @file device_mqtt.c
 * @brief MQTT 下发消息处理（P4-10 批次2b 从 device_service.c 拆出，只搬不改）。
 *
 * 内容：解析服务端下发主题（任务下发 / 配置更新）并驱动应用层服务。
 */

#include "application/device_service.h"
#include "application/device_internal.h"
#include "application/patrol_service.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        // Create patrol_task_t
        patrol_task_t *task = (patrol_task_t *)xcalloc_try(1, sizeof(patrol_task_t));
        if (!task) {
            LOG_ERROR("分配巡检任务失败（内存不足），丢弃该 MQTT 任务消息");
            cJSON_Delete(json);
            return;
        }
        
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
            task->points = (patrol_point_t *)xcalloc_try(1, sizeof(patrol_point_t));
            if (!task->points) {
                LOG_ERROR("分配巡检点失败（内存不足），丢弃该 MQTT 任务消息");
                patrol_task_free(task);
                cJSON_Delete(json);
                return;
            }
            task->point_count = 1;
            task->points[0].target_distance = distance;
            task->points[0].action = PATROL_ACTION_MOVE_FORWARD;

            // 2. Populate actions (current executor support)
            // The executor in patrol_task.c only looks at task->actions, not points.
            // We need to convert distance to duration-based action.
            
            patrol_action_t action;
            memset(&action, 0, sizeof(patrol_action_t));
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
            patrol_action_t scan_action;
            memset(&scan_action, 0, sizeof(patrol_action_t));
            scan_action.type = PATROL_ACTION_RFID_SCAN;
            scan_action.duration_ms = 2000; // Scan for 2 seconds
            patrol_task_add_action(task, &scan_action);
            
            // Also add a stop action at the end for safety
            patrol_action_t stop_action;
            memset(&stop_action, 0, sizeof(patrol_action_t));
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
