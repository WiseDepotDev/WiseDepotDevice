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
#include "common/utils.h"
#include "common/xmalloc.h"
#include "common/wd_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* 动作类型名称映射 */
static const struct {
    patrol_action_type_t type;
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
    patrol_task_status_t status;
    const char *name;
} g_task_status_names[] = {
    {PATROL_TASK_STATUS_PENDING, "pending"},
    {PATROL_TASK_STATUS_RUNNING, "running"},
    {PATROL_TASK_STATUS_COMPLETED, "completed"},
    {PATROL_TASK_STATUS_FAILED, "failed"},
    {PATROL_TASK_STATUS_CANCELLED, "cancelled"}
};

/* 取消标志已移入 patrol_task_t.cancel_requested（P4-07）：不再有全局取消状态 */

/**
 * 获取动作类型名称
 */
const char *patrol_action_type_to_string(patrol_action_type_t type) {
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
patrol_action_type_t patrol_action_type_from_string(const char *str) {
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
const char *patrol_task_status_to_string(patrol_task_status_t status) {
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
patrol_task_t *patrol_task_create(const char *id, const char *name) {
    patrol_task_t *task = xcalloc_try(1, sizeof(patrol_task_t));
    if (!task) {
        LOG_ERROR("创建巡检任务失败（内存不足）");
        return NULL;
    }
    
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
wd_error_t patrol_task_add_action(patrol_task_t *task, const patrol_action_t *action) {
    if (!task || !action) {
        return WD_ERR_PARAM;
    }
    
    if (task->action_count >= PATROL_TASK_MAX_ACTIONS) {
        LOG_ERROR("Patrol task action list is full");
        return WD_ERR_FULL;
    }
    
    memcpy(&task->actions[task->action_count], action, sizeof(patrol_action_t));
    task->action_count++;
    
    return 0;
}

/**
 * 执行单个动作
 */
static int execute_action(const patrol_action_t *action) {
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
wd_error_t patrol_task_execute(patrol_task_t *task, patrol_task_callback_t callback, void *context) {
    if (!task) {
        return WD_ERR_PARAM;
    }
    
    if (task->status == PATROL_TASK_STATUS_RUNNING) {
        LOG_WARN("Patrol task is already running: %s", task->id);
        return WD_ERR_STATE;
    }
    
    if (task->action_count == 0) {
        LOG_WARN("Patrol task has no actions: %s", task->id);
        task->status = PATROL_TASK_STATUS_COMPLETED;
        return 0;
    }
    
    task->status = PATROL_TASK_STATUS_RUNNING;
    task->current_action_index = 0;
    task->error_message[0] = '\0';
    task->cancel_requested = false; /* P4-07：每次执行从"未取消"开始 */
    
    LOG_INFO("Starting patrol task: %s (%d actions)", task->id, task->action_count);
    
    for (uint8_t i = 0; i < task->action_count; i++) {
        if (task->cancel_requested) {
            LOG_INFO("Patrol task cancelled: %s", task->id);
            task->status = PATROL_TASK_STATUS_CANCELLED;
            motor_stop_all();
            return WD_ERR_CANCELED;
        }
        
        task->current_action_index = i;
        patrol_action_t *action = &task->actions[i];
        
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
                return WD_ERR_ACTION;
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
            return WD_ERR_ACTION;
        }
    }
    
    /* P4-07：末个动作执行期间被取消也要如实标记（否则会被当成 COMPLETED） */
    if (task->cancel_requested) {
        LOG_INFO("Patrol task cancelled: %s", task->id);
        task->status = PATROL_TASK_STATUS_CANCELLED;
        motor_stop_all();
        return WD_ERR_CANCELED;
    }

    task->status = PATROL_TASK_STATUS_COMPLETED;
    LOG_INFO("Patrol task completed: %s", task->id);
    
    return 0;
}

/**
 * 取消巡检任务
 */
wd_error_t patrol_task_cancel(patrol_task_t *task) {
    if (!task) {
        return WD_ERR_PARAM;
    }
    
    if (task->status != PATROL_TASK_STATUS_RUNNING) {
        LOG_WARN("Cannot cancel task that is not running: %s", task->id);
        return WD_ERR_STATE;
    }
    
    /* P4-07：只置本任务的标志；执行线程在动作之间/动作内部轮询它 */
    task->cancel_requested = true;
    return 0;
}

/**
 * 释放巡检任务资源
 */
void patrol_task_free(patrol_task_t *task) {
    if (task) {
        xfree(task);
    }
}
