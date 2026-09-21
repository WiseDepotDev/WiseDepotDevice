/**
 * @file patrol_task_json.c
 * @brief 巡检任务的 JSON 解析与序列化（P4-10 批次2c 从 patrol_task.c 拆出）。
 *
 * P4-16 改动：解析改用 **cJSON**，对齐服务端真实契约 `TaskMessage`
 * （`{taskId:Long, taskType:Short, targetDistance:Float, planId, warehouseId}`，
 * **没有 actions 数组**），同时兼容带 `actions` 数组的报文（与 `patrol_task_to_json` 对称）
 * 以及字符串形式的 `taskId` / `taskType`。
 *
 * 旧的手写解析有两处与服务端契约不符（P4-15 四步联调发现）：
 * 1. `taskId` / `taskType` 在服务端是**数字**，旧助手只取引号字符串 →
 *    id 退化成 "unknown"、name 退化成 "unnamed"，且默认动作也不会被加上（任务 0 动作）；
 * 2. `actions` 是数组，旧助手只会取引号字符串 → 永远取不到。
 */

#include "domain/patrol_task.h"
#include "common/logger.h"
#include "common/utils.h"
#include "common/xmalloc.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 服务端按 targetDistance 折算动作时的默认速度（与 device_mqtt.c 保持一致） */
#define PATROL_DEFAULT_SPEED_CM_S 20.0f
#define PATROL_FALLBACK_DURATION_MS 5000

/** 取对象里的整数字段，兼容数字与字符串两种写法 */
static int json_int_compat(const cJSON *obj, const char *key, int default_val) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
    if (cJSON_IsNumber(it)) {
        return it->valueint;
    }
    if (cJSON_IsString(it) && it->valuestring) {
        return atoi(it->valuestring); // NOLINT(cert-err34-c)：解析失败即回落 default_val
    }
    return default_val;
}

/** 取字符串字段；数字也接受（按十进制转字符串写入 out）；缺省时写 default_val */
static void json_str_compat(const cJSON *obj, const char *key, char *out, size_t out_len,
                            const char *default_val) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
    if (cJSON_IsString(it) && it->valuestring && it->valuestring[0]) {
        snprintf(out, out_len, "%s", it->valuestring);
        return;
    }
    if (cJSON_IsNumber(it)) {
        snprintf(out, out_len, "%lld", (long long)it->valuedouble);
        return;
    }
    snprintf(out, out_len, "%s", default_val ? default_val : "");
}

/**
 * 从JSON字符串解析巡检任务
 */
patrol_task_t *patrol_task_from_json(const char *json) {
    if (!json) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        LOG_WARN("巡检任务 JSON 解析失败（不是合法 JSON）");
        return NULL;
    }
    if (!cJSON_IsObject(root)) {
        LOG_WARN("巡检任务 JSON 顶层不是对象");
        cJSON_Delete(root);
        return NULL;
    }

    char id_buf[32] = "";
    json_str_compat(root, "taskId", id_buf, sizeof(id_buf), "");
    if (id_buf[0] == '\0') {
        json_str_compat(root, "id", id_buf, sizeof(id_buf), "unknown");
    }

    char name_buf[64] = "";
    json_str_compat(root, "taskName", name_buf, sizeof(name_buf), "");
    if (name_buf[0] == '\0') {
        json_str_compat(root, "name", name_buf, sizeof(name_buf), "unnamed");
    }

    patrol_task_t *task = patrol_task_create(id_buf, name_buf);
    if (!task) {
        cJSON_Delete(root);
        return NULL;
    }

    /* taskType：服务端是数字（0=计划 1=手动），也接受字符串 "PLAN"/"MANUAL" */
    const cJSON *task_type = cJSON_GetObjectItemCaseSensitive(root, "taskType");
    if (cJSON_IsNumber(task_type)) {
        task->type = (task_type->valueint == 0) ? PATROL_TASK_TYPE_PLAN : PATROL_TASK_TYPE_MANUAL;
    } else if (cJSON_IsString(task_type) && task_type->valuestring) {
        task->type = (strcmp(task_type->valuestring, "PLAN") == 0) ? PATROL_TASK_TYPE_PLAN
                                                                   : PATROL_TASK_TYPE_MANUAL;
    }

    /* 动作来源一：显式 actions 数组（与 patrol_task_to_json 对称；服务端当前不下发） */
    const cJSON *actions = cJSON_GetObjectItemCaseSensitive(root, "actions");
    if (cJSON_IsArray(actions)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, (cJSON *)actions) {
            if (task->action_count >= PATROL_TASK_MAX_ACTIONS) {
                LOG_WARN("巡检任务动作数超过上限 %d，其余被忽略", PATROL_TASK_MAX_ACTIONS);
                break;
            }
            patrol_action_t action = {0};
            const cJSON *type = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "type");
            if (cJSON_IsString(type) && type->valuestring) {
                action.type = patrol_action_type_from_string(type->valuestring);
            }
            action.speed = (uint8_t)json_int_compat(item, "speed", 50);
            /* 兼容 duration_ms 与 duration 两种键名 */
            action.duration_ms = (uint32_t)json_int_compat(
                item, "duration_ms", json_int_compat(item, "duration", 1000));
            action.servo_channel = (uint8_t)json_int_compat(item, "channel", 0);
            action.servo_angle = (uint8_t)json_int_compat(item, "angle", 90);
            patrol_task_add_action(task, &action);
        }
        cJSON_Delete(root);
        return task;
    }

    /* 动作来源二：服务端 TaskMessage 契约——没有 actions，按 targetDistance 折算一个前进动作。
     * 连目标距离也没有时退化为固定 5s 前进动作（保持历史行为，任务不至于 0 动作）。 */
    const cJSON *distance_item = cJSON_GetObjectItemCaseSensitive(root, "targetDistance");
    float distance = cJSON_IsNumber(distance_item) ? (float)distance_item->valuedouble : 0.0f;

    patrol_action_t default_action = {
        .type = PATROL_ACTION_MOVE_FORWARD,
        .speed = 50,
        .duration_ms = (distance > 0.0f)
                           ? (uint32_t)((distance / PATROL_DEFAULT_SPEED_CM_S) * 1000.0f)
                           : PATROL_FALLBACK_DURATION_MS,
    };
    patrol_task_add_action(task, &default_action);

    cJSON_Delete(root);
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
