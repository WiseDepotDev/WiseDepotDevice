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
#include "common/wd_error.h"
#include <stdbool.h>

/* 配置结构体 */
typedef struct {
    char *server_url;       /**< 服务端地址 */
    char *device_id;        /**< 设备唯一标识 */
    int heartbeat_interval; /**< 心跳间隔 (秒) */
    char *log_path;         /**< 日志文件路径 */
    log_level_t log_level;     /**< 日志级别 */
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
    int rfid_power;         /**< RFID 功率 (dBm)，初始化时下发（P4-12） */
    int rfid_address;       /**< RFID 读头地址 0x00-0xFE；0xFF = 广播（P4-12） */
    bool rfid_legacy_frames;/**< true = 沿用旧帧路径（P4-12 兼容开关） */

    // 请求签名（P4-04）
    char *signature_secret; /**< X-Signature 的 HMAC 密钥；仅来自环境变量/配置文件，源码内无默认值 */
} wd_config_t;

/**
 * 加载配置
 * 优先级: 环境变量 > 配置文件 > 默认值
 *
 * 本函数**只做本地 I/O**（环境变量 + 可选配置文件），不发起任何网络请求——
 * 远端配置拉取已迁至 application 层的 `config_service_fetch_remote()`（见
 * include/application/config_service.h），以保证单元测试调用 config_load() 时
 * 不会产生网络副作用（P4-02），且 common 层不依赖 infrastructure（P4-08）。
 *
 * @param config_file 配置文件路径 (可选)
 * @return 0 成功，-1 失败
 */
/**
 * @brief 加载配置（环境变量 > 配置文件 > 默认值），只做本地 I/O
 * @param config_file 参数
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t config_load(const char *config_file);

/**
 * 更新配置 (从 JSON 字符串)
 *
 * @param json_str JSON 格式的配置字符串
 * @return 0 成功，-1 失败
 */
/**
 * @brief 用服务端下发的 JSON 更新配置并落盘
 * @param json_str 标准信封 JSON 文本
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t config_update_from_json(const char *json_str);

/**
 * 保存配置到加密文件
 *
 * @return 0 成功，-1 失败
 */
/**
 * @brief 把当前配置序列化落盘（当前为明文 JSON，加密待实现）
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t config_save_encrypted(void);

/**
 * 安全删除旧配置文件
 *
 * @param file_path 文件路径
 * @return 0 成功，-1 失败
 */
/**
 * @brief 安全删除配置文件
 * @param file_path 文件路径
 * @return WD_OK 成功；其余为负的错误码（见 common/wd_error.h）
 */
wd_error_t config_secure_delete(const char *file_path);

/**
 * 获取全局配置实例 (只读)
 *
 * @return 配置指针
 */
const wd_config_t *config_get(void);

/**
 * 请求签名密钥（P4-04：全仓唯一来源）
 *
 * 取值顺序：环境变量 `WISE_API_SIGNATURE_SECRET` > 配置文件 `signature_secret` > NULL。
 * 源码内**不保留任何默认密钥**（与 MQTT 口令、加密密钥同一策略，STD-SEC-01）；
 * 未配置时返回 NULL，调用方必须记录错误并**中止**签名请求，禁止退化为"无签名/固定密钥"。
 *
 * @return 密钥字符串（只读，不应打印到日志），未配置返回 NULL
 */
const char *config_signature_secret(void);

/**
 * 覆盖服务端地址（服务发现成功后回写配置；P4-10：原先只在调用方函数体内 extern）
 *
 * @param url 新的服务端地址（NULL 忽略）
 */
/**
 * @brief 覆盖服务端地址
 * @param url 参数
 */
void config_set_server_url(const char *url);

/**
 * 释放配置资源
 */
/**
 * @brief 释放配置结构占用的内存
 */
void config_free(void);

/**
 * 根据距离计算移动所需时间
 *
 * @param distance_cm 距离 (cm)
 * @return 时间 (秒)
 */
/**
 * @brief 按速度与距离计算行走所需毫秒数
 * @param distance_cm 距离（厘米）
 * @return 计算出的毫秒数（至少 1）
 */
float config_calculate_move_duration(float distance_cm);

#endif // WISE_DEPOT_CONFIG_H
