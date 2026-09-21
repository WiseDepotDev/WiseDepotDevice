/**
 * @file patrol_task_json.c
 * @brief 巡检任务的 JSON 解析与序列化（P4-10 批次2c 从 patrol_task.c 拆出，只搬不改）。
 *
 * 依赖 patrol_task.c 提供的公开构造/类型转换 API，不改动任何对外行为。
 */

#include "domain/patrol_task.h"
#include "common/logger.h"
#include "common/utils.h"
#include "common/xmalloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char *val = xmalloc_try(len + 1);
    if (!val) return NULL; /* P4-06：分配失败返回 NULL */
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
patrol_task_t *patrol_task_from_json(const char *json) {
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
    
    patrol_task_t *task = patrol_task_create(id ? id : "unknown", name ? name : "unnamed");
    
    if (id) xfree(id);
    if (name) xfree(name);
    
    char *actions_str = extract_json_string(json, "actions");
    if (!actions_str) {
        char *task_type = extract_json_string(json, "taskType");
        if (task_type) {
            patrol_action_t default_action = {
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
        char *action_str = xmalloc_try(action_len + 1);
        if (!action_str) {
            LOG_ERROR("分配动作串失败（内存不足），放弃本次任务解析");
            patrol_task_free(task);
            return NULL;
        }
        strncpy(action_str, action_start, action_len);
        action_str[action_len] = '\0';
        
        patrol_action_t action = {0};
        
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
char *patrol_task_to_json(const patrol_task_t *task) {
    if (!task) return NULL;

    size_t buf_size = 4096;
    char *json = xmalloc_try(buf_size);
    if (!json) {
        LOG_ERROR("分配任务 JSON 缓冲失败（内存不足）");
        return NULL;
    }
    size_t used = 0;

    /* P4-01：action_count 未校验时会越界读 actions[]（该数组上限为 PATROL_TASK_MAX_ACTIONS），
     * 这里先夹取到数组真实容量，避免用损坏/伪造的计数去索引。 */
    uint8_t action_count = task->action_count;
    if (action_count > PATROL_TASK_MAX_ACTIONS) {
        LOG_WARN("patrol_task_to_json: action_count=%u exceeds max %d, clamped",
                 (unsigned)action_count, PATROL_TASK_MAX_ACTIONS);
        action_count = PATROL_TASK_MAX_ACTIONS;
    }

    /* P4-01：原写法 `offset += snprintf(json + offset, buf_size - offset, ...)` 有两处隐患：
     * 1. snprintf 返回**期望长度**，截断后 offset 会超过 buf_size，随后 `buf_size - offset`
     *    在 size_t 下溢成巨大值 → 越界写；
     * 2. `json + offset` 在越界后本身就是非法指针（UB）。
     * 改为 wd_str_appendf()：容量内累加，任何情况下都不越界且保持 NUL 结尾。 */
    used = wd_str_appendf(json, buf_size, used,
                          "{\"id\":\"%s\",\"name\":\"%s\",\"status\":\"%s\",\"actionCount\":%d,\"actions\":[",
                          task->id, task->name,
                          patrol_task_status_to_string(task->status),
                          action_count);

    for (uint8_t i = 0; i < action_count; i++) {
        const patrol_action_t *action = &task->actions[i];

        if (i > 0) {
            used = wd_str_appendf(json, buf_size, used, ",");
        }

        used = wd_str_appendf(json, buf_size, used,
                              "{\"type\":\"%s\",\"speed\":%d,\"duration\":%u",
                              patrol_action_type_to_string(action->type),
                              action->speed, action->duration_ms);

        if (action->type == PATROL_ACTION_SERVO) {
            used = wd_str_appendf(json, buf_size, used,
                                  ",\"channel\":%d,\"angle\":%d",
                                  action->servo_channel, action->servo_angle);
        }

        used = wd_str_appendf(json, buf_size, used, "}");
    }

    used = wd_str_appendf(json, buf_size, used, "]}");
    json[used < buf_size ? used : buf_size - 1] = '\0';

    return json;
}
