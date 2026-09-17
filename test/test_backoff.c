#include "common/backoff_strategy.h"
#include <assert.h>
#include <stdio.h>

void test_backoff_strategy(void) {
    BackoffState state;
    BackoffConfig config = {
        .initial_interval_ms = 1000,
        .max_interval_ms = 10000,
        .multiplier = 2.0,
        .jitter = 0.0 // Disable jitter for predictable test
    };
    
    // 1. Init
    backoff_init(&state, &config);
    assert(state.current_interval_ms == 1000);
    assert(state.attempts == 0);
    
    // 2. Next Interval (1000 * 2 = 2000)
    // Note: next_interval returns CURRENT then updates NEXT
    unsigned int wait1 = backoff_next_interval(&state);
    assert(wait1 == 1000);
    assert(state.current_interval_ms == 2000);
    assert(state.attempts == 1);
    
    // 3. Next (2000)
    unsigned int wait2 = backoff_next_interval(&state);
    assert(wait2 == 2000);
    assert(state.current_interval_ms == 4000);
    
    // 4. Max Cap Test
    // 4000 -> 8000
    backoff_next_interval(&state);
    // 8000 -> 10000 (capped)
    unsigned int wait4 = backoff_next_interval(&state);
    assert(wait4 == 8000);
    assert(state.current_interval_ms == 10000);
    
    // 5. Reset
    backoff_reset(&state);
    assert(state.current_interval_ms == 1000);
    assert(state.attempts == 0);
    
    printf("[TEST] Backoff strategy logic passed.\n");
}
