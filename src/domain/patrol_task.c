/**
 * 巡检任务模块实现
 * 
 * 定义巡检任务的数据结构和操作接口
 * 
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-12
 */

#include "domain/patrol_task.h"
#include "domain/motor_controller.h"
#include "common/logger.h"
#include "common/xmalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* 动作类型名称映射 */
static const struct {
    PatrolActionType type;
    const char *name;
} g_action_type_names[] = {
    {PATROL_ACTION_MOVE_FORWARD, "move_forward"},
    {PATROL_ACTION_MOVE_BACKWARD, "move_backward"},
    {PATROL_ACTION_MOVE_LEFT, "move_left"},
    {PATROL_ACTION_MOVE_RIGHT, "move_right"},
    {PATROL_ACTION_TURN_LEFT, "turn_left"},
    {PATROL_ACTION_TURN_RIGHT, "turn_right"},
    {PATROL_ACTION_MOVE_FORWARD_LEFT, "move_forward_left"},
    {PATROL_ACTION_MOVE_FORWARD_RIGHT, "move_forward_right"},
    {PATROL_ACTION_MOVE_BACKWARD_LEFT, "move_backward_left"},
    {PATROL_ACTION_MOVE_BACKWARD_RIGHT, "move_backward_right"},
    {PATROL_ACTION_STOP, "stop"},
    {PATROL_ACTION_WAIT, "wait"},
    {PATROL_ACTION_SERVO, "servo"},
    {PATROL_ACTION_RFID_SCAN, "rfid_scan"},
    {PATROL_ACTION_UNKNOWN, "unknown"}
};

/* 任务状态名称映射 */
static const struct {
    PatrolTaskStatus status;
    const char *name;
} g_task_status_names[] = {
    {PATROL_TASK_STATUS_PENDING, "pending"},
    {PATROL_TASK_STATUS_RUNNING, "running"},
    {PATROL_TASK_STATUS_COMPLETED, "completed"},
    {PATROL_TASK_STATUS_FAILED, "failed"},
    {PATROL_TASK_STATUS_CANCELLED, "cancelled"}
};

/* 全局取消标志 */
static volatile bool g_cancel_flag = false;

/**
 * 获取动作类型名称
 */
const char *patrol_action_type_to_string(PatrolActionType type) {
    for (size_t i = 0; i < sizeof(g_action_type_names) / sizeof(g_action_type_names[0]); i++) {
        if (g_action_type_names[i].type == type) {
            return g_action_type_names[i].name;
        }
    }
    return "unknown";
}

/**
 * 从字符串解析动作类型
 */
PatrolActionType patrol_action_type_from_string(const char *str) {
    if (!str) return PATROL_ACTION_UNKNOWN;
    
    for (size_t i = 0; i < sizeof(g_action_type_names) / sizeof(g_action_type_names[0]); i++) {
        if (strcmp(g_action_type_names[i].name, str) == 0) {
            return g_action_type_names[i].type;
        }
    }
    return PATROL_ACTION_UNKNOWN;
}

/**
 * 获取任务状态名称
 */
const char *patrol_task_status_to_string(PatrolTaskStatus status) {
    for (size_t i = 0; i < sizeof(g_task_status_names) / sizeof(g_task_status_names[0]); i++) {
        if (g_task_status_names[i].status == status) {
            return g_task_status_names[i].name;
        }
    }
    return "unknown";
}

/**
 * 创建空的巡检任务
 */
PatrolTask *patrol_task_create(const char *id, const char *name) {
    PatrolTask *task = xcalloc(1, sizeof(PatrolTask));
    
    if (id) {
        strncpy(task->id, id, PATROL_TASK_ID_MAX_LEN - 1);
        task->id[PATROL_TASK_ID_MAX_LEN - 1] = '\0';
    }
    
    if (name) {
        strncpy(task->name, name, PATROL_TASK_NAME_MAX_LEN - 1);
        task->name[PATROL_TASK_NAME_MAX_LEN - 1] = '\0';
    }
    
    task->status = PATROL_TASK_STATUS_PENDING;
    task->action_count = 0;
    task->current_action_index = 0;
    task->error_message[0] = '\0';
    
    return task;
}

/**
 * 向任务添加动作
 */
int patrol_task_add_action(PatrolTask *task, const PatrolAction *action) {
    if (!task || !action) {
        return -1;
    }
    
    if (task->action_count >= PATROL_TASK_MAX_ACTIONS) {
        LOG_ERROR("Patrol task action list is full");
        return -1;
    }
    
    memcpy(&task->actions[task->action_count], action, sizeof(PatrolAction));
    task->action_count++;
    
    return 0;
}

/**
 * 执行单个动作
 */
static int execute_action(const PatrolAction *action) {
    int result = 0;
    
    switch (action->type) {
        case PATROL_ACTION_MOVE_FORWARD:
            result = motor_move(MOVE_FORWARD, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_BACKWARD:
            result = motor_move(MOVE_BACKWARD, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_LEFT:
            result = motor_move(MOVE_LEFT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_RIGHT:
            result = motor_move(MOVE_RIGHT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_TURN_LEFT:
            result = motor_move(MOVE_TURN_LEFT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_TURN_RIGHT:
            result = motor_move(MOVE_TURN_RIGHT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_FORWARD_LEFT:
            result = motor_move(MOVE_FORWARD_LEFT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_FORWARD_RIGHT:
            result = motor_move(MOVE_FORWARD_RIGHT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_BACKWARD_LEFT:
            result = motor_move(MOVE_BACKWARD_LEFT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_MOVE_BACKWARD_RIGHT:
            result = motor_move(MOVE_BACKWARD_RIGHT, action->speed, action->duration_ms);
            break;
            
        case PATROL_ACTION_STOP:
            motor_stop_all();
            if (action->duration_ms > 0) {
                struct timespec ts = {
                    .tv_sec = (action->duration_ms * 1000000L) / 1000000000L,
                    .tv_nsec = (action->duration_ms * 1000000L) % 1000000000L
                };
                nanosleep(&ts, NULL);
            }
            break;
            
        case PATROL_ACTION_WAIT:
            {
                struct timespec ts = {
                    .tv_sec = (action->duration_ms * 1000000L) / 1000000000L,
                    .tv_nsec = (action->duration_ms * 1000000L) % 1000000000L
                };
                nanosleep(&ts, NULL);
            }
            break;
            
        case PATROL_ACTION_SERVO:
            result = servo_set_angle(action->servo_channel, action->servo_angle);
            if (result == 0 && action->duration_ms > 0) {
                struct timespec ts = {
                    .tv_sec = (action->duration_ms * 1000000L) / 1000000000L,
                    .tv_nsec = (action->duration_ms * 1000000L) % 1000000000L
                };
                nanosleep(&ts, NULL);
            }
            break;
            
        case PATROL_ACTION_RFID_SCAN:
            // Domain layer treats this as a placeholder. Actual execution should be handled by callback.
            // If duration is specified, we wait.
            if (action->duration_ms > 0) {
                struct timespec ts = {
                    .tv_sec = (action->duration_ms * 1000000L) / 1000000000L,
                    .tv_nsec = (action->duration_ms * 1000000L) % 1000000000L
                };
                nanosleep(&ts, NULL);
            }
            break;

        default:
            LOG_ERROR("Unknown patrol action type: %d", action->type);
            result = -1;
            break;
    }
    
    return result;
}

/**
 * 执行巡检任务
 */
int patrol_task_execute(PatrolTask *task, PatrolTaskCallback callback, void *context) {
    if (!task) {
        return -1;
    }
    
    if (task->status == PATROL_TASK_STATUS_RUNNING) {
        LOG_WARN("Patrol task is already running: %s", task->id);
        return -1;
    }
    
    if (task->action_count == 0) {
        LOG_WARN("Patrol task has no actions: %s", task->id);
        task->status = PATROL_TASK_STATUS_COMPLETED;
        return 0;
    }
    
    task->status = PATROL_TASK_STATUS_RUNNING;
    task->current_action_index = 0;
    task->error_message[0] = '\0';
    g_cancel_flag = false;
    
    LOG_INFO("Starting patrol task: %s (%d actions)", task->id, task->action_count);
    
    for (uint8_t i = 0; i < task->action_count; i++) {
        if (g_cancel_flag) {
            LOG_INFO("Patrol task cancelled: %s", task->id);
            task->status = PATROL_TASK_STATUS_CANCELLED;
            motor_stop_all();
            return -1;
        }
        
        task->current_action_index = i;
        PatrolAction *action = &task->actions[i];
        
        LOG_DEBUG("Executing action %d/%d: %s (speed: %d%%, duration: %ums)",
                  i + 1, task->action_count,
                  patrol_action_type_to_string(action->type),
                  action->speed, action->duration_ms);
        
        if (callback) {
            int cb_res = callback(task, i, context);
            if (cb_res < 0) {
                snprintf(task->error_message, sizeof(task->error_message),
                         "Action %d failed (callback): %s", i + 1, patrol_action_type_to_string(action->type));
                LOG_ERROR("Patrol task action failed in callback");
                task->status = PATROL_TASK_STATUS_FAILED;
                motor_stop_all();
                return -1;
            }
            if (cb_res > 0) {
                continue; // Handled by callback
            }
        }
        
        if (execute_action(action) < 0) {
            snprintf(task->error_message, sizeof(task->error_message),
                     "Action %d failed: %s", i + 1, patrol_action_type_to_string(action->type));
            LOG_ERROR("Patrol task action failed: %s", task->error_message);
            task->status = PATROL_TASK_STATUS_FAILED;
            motor_stop_all();
            return -1;
        }
    }
    
    task->status = PATROL_TASK_STATUS_COMPLETED;
    LOG_INFO("Patrol task completed: %s", task->id);
    
    return 0;
}

/**
 * 取消巡检任务
 */
int patrol_task_cancel(PatrolTask *task) {
    if (!task) {
        return -1;
    }
    
    if (task->status != PATROL_TASK_STATUS_RUNNING) {
        LOG_WARN("Cannot cancel task that is not running: %s", task->id);
        return -1;
    }
    
    g_cancel_flag = true;
    return 0;
}

/**
 * 释放巡检任务资源
 */
void patrol_task_free(PatrolTask *task) {
    if (task) {
        xfree(task);
    }
}

/**
 * 简单的JSON字符串提取工具
 */
static char *extract_json_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    char *p = strstr(json, search_key);
    if (!p) return NULL;
    
    p = strchr(p, ':');
    if (!p) return NULL;
    
    char *start = strchr(p, '"');
    if (!start) return NULL;
    start++;
    
    char *end = strchr(start, '"');
    if (!end) return NULL;
    
    size_t len = end - start;
    char *val = xmalloc(len + 1);
    strncpy(val, start, len);
    val[len] = '\0';
    
    return val;
}

/**
 * 提取JSON数字
 */
static int extract_json_int(const char *json, const char *key, int default_val) {
    if (!json || !key) return default_val;
    
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    char *p = strstr(json, search_key);
    if (!p) return default_val;
    
    p = strchr(p, ':');
    if (!p) return default_val;
    
    while (*p && (*p == ':' || *p == ' ' || *p == '\t')) p++;
    
    return atoi(p);
}

/**
 * 从JSON字符串解析巡检任务
 */
PatrolTask *patrol_task_from_json(const char *json) {
    if (!json) return NULL;
    
    char *id = extract_json_string(json, "taskId");
    char *name = extract_json_string(json, "taskName");
    
    if (!id) {
        id = extract_json_string(json, "id");
    }
    if (!name) {
        name = extract_json_string(json, "taskType");
        if (!name) {
            name = extract_json_string(json, "name");
        }
    }
    
    PatrolTask *task = patrol_task_create(id ? id : "unknown", name ? name : "unnamed");
    
    if (id) xfree(id);
    if (name) xfree(name);
    
    char *actions_str = extract_json_string(json, "actions");
    if (!actions_str) {
        char *task_type = extract_json_string(json, "taskType");
        if (task_type) {
            PatrolAction default_action = {
                .type = PATROL_ACTION_MOVE_FORWARD,
                .speed = 50,
                .duration_ms = 5000
            };
            patrol_task_add_action(task, &default_action);
            xfree(task_type);
        }
        return task;
    }
    
    char *p = actions_str;
    while (*p && task->action_count < PATROL_TASK_MAX_ACTIONS) {
        char *action_start = strchr(p, '{');
        if (!action_start) break;
        
        char *action_end = strchr(action_start, '}');
        if (!action_end) break;
        
        size_t action_len = action_end - action_start + 1;
        char *action_str = xmalloc(action_len + 1);
        strncpy(action_str, action_start, action_len);
        action_str[action_len] = '\0';
        
        PatrolAction action = {0};
        
        char *type_str = extract_json_string(action_str, "type");
        if (type_str) {
            action.type = patrol_action_type_from_string(type_str);
            xfree(type_str);
        }
        
        action.speed = (uint8_t)extract_json_int(action_str, "speed", 50);
        action.duration_ms = (uint32_t)extract_json_int(action_str, "duration", 1000);
        action.servo_channel = (uint8_t)extract_json_int(action_str, "channel", 0);
        action.servo_angle = (uint8_t)extract_json_int(action_str, "angle", 90);
        
        patrol_task_add_action(task, &action);
        
        xfree(action_str);
        p = action_end + 1;
    }
    
    xfree(actions_str);
    return task;
}

/**
 * 将巡检任务转换为JSON字符串
 */
char *patrol_task_to_json(const PatrolTask *task) {
    if (!task) return NULL;
    
    size_t buf_size = 4096;
    char *json = xmalloc(buf_size);
    int offset = 0;
    
    offset += snprintf(json + offset, buf_size - offset,
                       "{\"id\":\"%s\",\"name\":\"%s\",\"status\":\"%s\",\"actionCount\":%d,\"actions\":[",
                       task->id, task->name,
                       patrol_task_status_to_string(task->status),
                       task->action_count);
    
    for (uint8_t i = 0; i < task->action_count; i++) {
        const PatrolAction *action = &task->actions[i];
        
        if (i > 0) {
            offset += snprintf(json + offset, buf_size - offset, ",");
        }
        
        offset += snprintf(json + offset, buf_size - offset,
                           "{\"type\":\"%s\",\"speed\":%d,\"duration\":%u",
                           patrol_action_type_to_string(action->type),
                           action->speed, action->duration_ms);
        
        if (action->type == PATROL_ACTION_SERVO) {
            offset += snprintf(json + offset, buf_size - offset,
                               ",\"channel\":%d,\"angle\":%d",
                               action->servo_channel, action->servo_angle);
        }
        
        offset += snprintf(json + offset, buf_size - offset, "}");
    }
    
    offset += snprintf(json + offset, buf_size - offset, "]}");
    
    return json;
}
