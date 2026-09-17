/**
 * 重连策略模块实现
 *
 * @author xingchentye
 * @version 0.4.0
 * @since 2026-03-06
 */

#include "common/backoff_strategy.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 默认配置 */
static const BackoffConfig DEFAULT_CONFIG = {
    .initial_interval_ms = 1000,
    .max_interval_ms = 300000, // 5 minutes
    .multiplier = 2.0,
    .jitter = 0.2
};

void backoff_init(BackoffState *state, const BackoffConfig *config) {
    if (!state) return;
    
    if (config) {
        state->config = *config;
    } else {
        state->config = DEFAULT_CONFIG;
    }
    
    // Validate config
    if (state->config.initial_interval_ms == 0) state->config.initial_interval_ms = 100;
    if (state->config.multiplier < 1.0) state->config.multiplier = 1.0;
    if (state->config.jitter < 0.0) state->config.jitter = 0.0;
    if (state->config.jitter > 1.0) state->config.jitter = 1.0;
    
    backoff_reset(state);
    
    // Seed random number generator if not already done
    // Note: In multi-threaded environment, this should be done once globally.
    // For this simple implementation, we assume main() called srand or we do it here lazily.
    // However, calling srand repeatedly is bad.
    // Let's assume user calls srand(time(NULL)) in main.
}

void backoff_reset(BackoffState *state) {
    if (!state) return;
    state->current_interval_ms = state->config.initial_interval_ms;
    state->attempts = 0;
}

unsigned int backoff_next_interval(BackoffState *state) {
    if (!state) return 0;
    
    unsigned int interval = state->current_interval_ms;
    
    // Apply jitter: interval * (1 +/- jitter)
    // Random float between -jitter and +jitter
    if (state->config.jitter > 0.0001) {
        double r = (double)rand() / RAND_MAX; // 0.0 to 1.0
        double j = (r * 2.0 * state->config.jitter) - state->config.jitter;
        interval = (unsigned int)(interval * (1.0 + j));
    }
    
    // Calculate next interval for next call
    unsigned int next = (unsigned int)(state->current_interval_ms * state->config.multiplier);
    if (next > state->config.max_interval_ms) {
        next = state->config.max_interval_ms;
    }
    state->current_interval_ms = next;
    state->attempts++;
    
    return interval;
}
