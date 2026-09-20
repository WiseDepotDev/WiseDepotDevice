/**
 * 配置管理模块头文件
 *
 * @author xingchentye
 * @version 0.2.0
 * @since 2026-03-13
 */

#ifndef WISE_DEPOT_CONFIG_H
#define WISE_DEPOT_CONFIG_H

#include "common/logger.h"
#include <stdbool.h>

/* 配置结构体 */
typedef struct {
    char *server_url;       /**< 服务端地址 */
    char *device_id;        /**< 设备唯一标识 */
    int heartbeat_interval; /**< 心跳间隔 (秒) */
    char *log_path;         /**< 日志文件路径 */
    LogLevel log_level;     /**< 日志级别 */
    float move_speed_cm_s;  /**< 移动速度 (cm/s) */
    
    // 电机微调参数 (-1.0 到 1.0，正数增加速度，负数减少速度)
    float motor_trim_a;     /**< 左前电机微调 */
    float motor_trim_b;     /**< 右前电机微调 */
    float motor_trim_c;     /**< 左后电机微调 */
    float motor_trim_d;     /**< 右后电机微调 */
    
    // 动态配置
    char *api_base_url;     /**< API 基础地址 */
    int task_poll_interval; /**< 任务轮询间隔 (秒) */
    int network_timeout;    /**< 网络超时 (ms) */
    char *log_upload_strategy; /**< 日志上传策略 */
    char *encryption_key;   /**< 加密密钥 */
    char *version;          /**< 配置版本 */

    // MQTT 配置
    char *mqtt_host;        /**< MQTT 服务器地址 */
    int mqtt_port;          /**< MQTT 端口 */
    char *mqtt_username;    /**< MQTT 用户名 */
    char *mqtt_password;    /**< MQTT 密码 */

    // RFID 配置
    char *rfid_serial_port; /**< RFID 串口设备 */
    int rfid_baudrate;      /**< RFID 波特率 */
    int rfid_power;         /**< RFID 功率 (dBm) */
} Config;

/**
 * 加载配置
 * 优先级: 环境变量 > 配置文件 > 默认值
 *
 * 本函数**只做本地 I/O**（环境变量 + 可选配置文件），不发起任何网络请求——
 * 远端配置拉取已拆到 config_fetch_remote()，由 application 层显式调用，
 * 以保证单元测试调用 config_load() 时不会产生网络副作用（P4-02）。
 *
 * @param config_file 配置文件路径 (可选)
 * @return 0 成功，-1 失败
 */
int config_load(const char *config_file);

/**
 * 从服务端拉取配置并合并（可选步骤）
 *
 * 需要 server_url 与 device_id 已就绪；失败只记录告警、不改变已有配置。
 * 由 application 层（P4-08 规划迁至 application 层）在 config_load() 之后调用。
 *
 * @return 0 成功拉取并合并，-1 跳过或失败
 */
int config_fetch_remote(void);

/**
 * 更新配置 (从 JSON 字符串)
 *
 * @param json_str JSON 格式的配置字符串
 * @return 0 成功，-1 失败
 */
int config_update_from_json(const char *json_str);

/**
 * 保存配置到加密文件
 *
 * @return 0 成功，-1 失败
 */
int config_save_encrypted(void);

/**
 * 安全删除旧配置文件
 *
 * @param file_path 文件路径
 * @return 0 成功，-1 失败
 */
int config_secure_delete(const char *file_path);

/**
 * 获取全局配置实例 (只读)
 *
 * @return 配置指针
 */
const Config *config_get(void);

/**
 * 释放配置资源
 */
void config_free(void);

/**
 * 根据距离计算移动所需时间
 *
 * @param distance_cm 距离 (cm)
 * @return 时间 (秒)
 */
float config_calculate_move_duration(float distance_cm);

#endif // WISE_DEPOT_CONFIG_H
