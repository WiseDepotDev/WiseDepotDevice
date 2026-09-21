/**
 * 巡检任务模块头文件
 * 
 * 定义巡检任务的数据结构和操作接口
 * 
 * @author xingchentye
 * @version 1.0.0
 * @since 2026-03-12
 */

#ifndef WISE_DEPOT_PATROL_TASK_H
#define WISE_DEPOT_PATROL_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/motor_controller.h"
#include "common/wd_error.h"

/* 最大任务名称长度 */
#define PATROL_TASK_NAME_MAX_LEN 64
/* 最大动作数量 */
#define PATROL_TASK_MAX_ACTIONS 32
/* 最大任务ID长度 */
#define PATROL_TASK_ID_MAX_LEN 64

/**
 * 巡检任务类型枚举
 */
typedef enum {
    PATROL_TASK_TYPE_PLAN = 0,    /**< 计划任务 */
    PATROL_TASK_TYPE_MANUAL = 1   /**< 手动任务 */
} patrol_task_type_t;

/**
 * 巡检动作类型枚举
 */
typedef enum {
    PATROL_ACTION_MOVE,                /**< 移动 (通用) */
    PATROL_ACTION_MOVE_FORWARD,        /**< 前进 */
    PATROL_ACTION_MOVE_BACKWARD,       /**< 后退 */
    PATROL_ACTION_MOVE_LEFT,           /**< 左移 */
    PATROL_ACTION_MOVE_RIGHT,          /**< 右移 */
    PATROL_ACTION_TURN_LEFT,           /**< 左转 */
    PATROL_ACTION_TURN_RIGHT,          /**< 右转 */
    PATROL_ACTION_MOVE_FORWARD_LEFT,   /**< 前左斜 */
    PATROL_ACTION_MOVE_FORWARD_RIGHT,  /**< 前右斜 */
    PATROL_ACTION_MOVE_BACKWARD_LEFT,  /**< 后左斜 */
    PATROL_ACTION_MOVE_BACKWARD_RIGHT, /**< 后右斜 */
    PATROL_ACTION_STOP,                /**< 停止 */
    PATROL_ACTION_WAIT,                /**< 等待 */
    PATROL_ACTION_SERVO,               /**< 舵机控制 */
    PATROL_ACTION_RFID_SCAN,           /**< RFID 盘点 */
    PATROL_ACTION_UNKNOWN              /**< 未知动作 */
} patrol_action_type_t;

/**
 * 巡检动作结构体
 */
typedef struct {
    patrol_action_type_t type;     /**< 动作类型 */
    uint8_t speed;             /**< 速度百分比 (0-100) */
    uint32_t duration_ms;      /**< 持续时间 (毫秒) */
    uint8_t servo_channel;     /**< 舵机通道 (仅SERVO类型有效) */
    uint8_t servo_angle;       /**< 舵机角度 (仅SERVO类型有效) */
} patrol_action_t;

/**
 * 巡检任务状态枚举
 */
typedef enum {
    PATROL_TASK_STATUS_PENDING,    /**< 待执行 */
    PATROL_TASK_STATUS_RUNNING,    /**< 执行中 */
    PATROL_TASK_STATUS_COMPLETED,  /**< 已完成 */
    PATROL_TASK_STATUS_FAILED,     /**< 失败 */
    PATROL_TASK_STATUS_CANCELLED   /**< 已取消 */
} patrol_task_status_t;

/**
 * 巡检点结构体 (用于任务执行)
 */
typedef struct {
    float target_distance;     /**< 目标距离 (cm) */
    patrol_action_type_t action;   /**< 动作类型 */
    // 可根据需要添加 RFID 或其他检查点信息
} patrol_point_t;

/**
 * 巡检任务结构体
 *
 * 取消语义（P4-07）：取消标志是**每个任务自己的字段**（`cancel_requested`），
 * 不再使用全局变量——否则取消任务 A 会连带影响任务 B，且队列/多任务场景无法表达。
 * 该字段由执行线程读取、外部线程写入，声明为 volatile。
 */
typedef struct {
    char id[PATROL_TASK_ID_MAX_LEN];           /**< 任务ID */
    char name[PATROL_TASK_NAME_MAX_LEN];       /**< 任务名称 */
    patrol_task_type_t type;                       /**< 任务类型 */
    patrol_action_t actions[PATROL_TASK_MAX_ACTIONS]; /**< 动作列表 */
    uint8_t action_count;                      /**< 动作数量 */
    patrol_point_t *points;                       /**< 巡检点列表 (动态分配) */
    uint8_t point_count;                       /**< 巡检点数量 */
    patrol_task_status_t status;                   /**< 任务状态 */
    uint8_t current_action_index;              /**< 当前执行的动作索引 */
    volatile bool cancel_requested;            /**< 取消请求（P4-07：每任务独立，无全局） */
    char error_message[256];                   /**< 错误信息 */
} patrol_task_t;

/**
 * 巡检任务执行回调函数类型
 * 
 * @param task 当前执行的任务
 * @param action_index 当前动作索引
 * @param context 用户上下文
 * @return 0: 继续默认执行, 1: 已处理(跳过默认), -1: 错误(终止任务)
 */
typedef int (*patrol_task_callback_t)(const patrol_task_t *task, uint8_t action_index, void *context);

/**
 * 创建空的巡检任务
 * 
 * @param id 任务ID
 * @param name 任务名称
 * @return 新创建的任务结构体 (需要调用者释放)
 */
patrol_task_t *patrol_task_create(const char *id, const char *name);

/**
 * 向任务添加动作
 * 
 * @param task 任务指针
 * @param action 动作结构体
 * @return 0 成功，-1 失败 (任务已满)
 */
wd_error_t patrol_task_add_action(patrol_task_t *task, const patrol_action_t *action);

/**
 * 执行巡检任务
 * 
 * @param task 任务指针
 * @param callback 执行回调 (可为NULL)
 * @param context 回调上下文
 * @return 0 成功，-1 失败
 */
wd_error_t patrol_task_execute(patrol_task_t *task, patrol_task_callback_t callback, void *context);

/**
 * 取消巡检任务
 * 
 * @param task 任务指针
 * @return 0 成功，-1 失败
 */
wd_error_t patrol_task_cancel(patrol_task_t *task);

/**
 * 释放巡检任务资源
 * 
 * @param task 任务指针
 */
void patrol_task_free(patrol_task_t *task);

/**
 * 从JSON字符串解析巡检任务
 * 
 * @param json JSON字符串
 * @return 解析后的任务结构体 (需要调用者释放)，失败返回NULL
 */
patrol_task_t *patrol_task_from_json(const char *json);

/**
 * 将巡检任务转换为JSON字符串
 * 
 * @param task 任务指针
 * @return JSON字符串 (需要调用者释放)，失败返回NULL
 */
char *patrol_task_to_json(const patrol_task_t *task);

/**
 * 获取动作类型名称
 * 
 * @param type 动作类型
 * @return 动作类型名称字符串
 */
const char *patrol_action_type_to_string(patrol_action_type_t type);

/**
 * 从字符串解析动作类型
 * 
 * @param str 动作类型字符串
 * @return 动作类型枚举值
 */
patrol_action_type_t patrol_action_type_from_string(const char *str);

/**
 * 获取任务状态名称
 * 
 * @param status 任务状态
 * @return 任务状态名称字符串
 */
const char *patrol_task_status_to_string(patrol_task_status_t status);

#endif // WISE_DEPOT_PATROL_TASK_H
