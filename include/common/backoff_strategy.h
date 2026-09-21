/**
 * 重连策略模块头文件
 *
 * @author xingchentye
 * @version 0.4.0
 * @since 2026-03-06
 */

#ifndef WISE_DEPOT_BACKOFF_STRATEGY_H
#define WISE_DEPOT_BACKOFF_STRATEGY_H

/**
 * 重连策略配置
 */
typedef struct {
    unsigned int initial_interval_ms; /**< 初始间隔 (毫秒) */
    unsigned int max_interval_ms;     /**< 最大间隔 (毫秒) */
    double multiplier;                /**< 增长倍数 (如 2.0) */
    double jitter;                    /**< 随机抖动因子 (0.0 - 1.0) */
} backoff_config_t;

/**
 * 重连状态
 */
typedef struct {
    backoff_config_t config;
    unsigned int current_interval_ms;
    unsigned int attempts;
} backoff_state_t;

/**
 * 初始化重连策略
 *
 * @param state 状态结构体指针
 * @param config 配置结构体指针 (如果为 NULL，使用默认值)
 */
void backoff_init(backoff_state_t *state, const backoff_config_t *config);

/**
 * 获取下一次重试间隔
 *
 * @param state 状态结构体指针
 * @return 等待毫秒数
 */
unsigned int backoff_next_interval(backoff_state_t *state);

/**
 * 重置重连状态 (连接成功后调用)
 *
 * @param state 状态结构体指针
 */
void backoff_reset(backoff_state_t *state);

#endif // WISE_DEPOT_BACKOFF_STRATEGY_H
