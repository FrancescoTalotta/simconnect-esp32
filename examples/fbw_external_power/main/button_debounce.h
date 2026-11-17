// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int candidate;
    int stable;
    bool armed;
    int64_t since_ms;
} button_debounce_t;

static inline void button_debounce_init(button_debounce_t *b, int level, int64_t now)
{
    *b = (button_debounce_t){ .candidate = level, .stable = level, .since_ms = now };
}

// Active-low. Always require a stable release first, including at startup.
static inline bool button_debounce_update(button_debounce_t *b, int level, int64_t now)
{
    if (level != b->candidate) {
        b->candidate = level;
        b->since_ms = now;
    }
    if (now - b->since_ms < 30) return false;
    if (b->candidate == 1) b->armed = true;
    if (b->stable == b->candidate) return false;
    b->stable = b->candidate;
    if (b->stable == 0 && b->armed) {
        b->armed = false;
        return true;
    }
    return false;
}
