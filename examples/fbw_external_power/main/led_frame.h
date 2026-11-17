// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <stdint.h>

// SK68XXMINI-HS: GRB, most-significant bit first, at 10 MHz RMT resolution.
// 0: 0.3 us high / 0.9 us low; 1: 0.9 us high / 0.3 us low.
// 300 us latch also covers newer SK6812 variants requiring >200 us.
enum {
    LED_RESOLUTION_HZ = 10000000,
    LED_BIT_TICKS = 12,
    LED_ZERO_HIGH_TICKS = 3,
    LED_ONE_HIGH_TICKS = 9,
    LED_RESET_HALF_TICKS = 1500,
    LED_FRAME_SYMBOLS = 25,
};

static inline void led_grb_bytes(uint8_t red, uint8_t green, uint8_t blue, uint8_t out[3])
{
    out[0] = green;
    out[1] = red;
    out[2] = blue;
}

static inline unsigned led_bit_high_ticks(uint8_t byte, unsigned bit)
{
    return (byte & (0x80u >> bit)) ? LED_ONE_HIGH_TICKS : LED_ZERO_HIGH_TICKS;
}
