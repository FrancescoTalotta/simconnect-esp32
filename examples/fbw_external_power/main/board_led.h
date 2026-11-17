// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "led_frame.h"

typedef struct {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    rmt_symbol_word_t symbols[LED_FRAME_SYMBOLS];
} board_led_t;

// Initialize/clear before starting the output task; afterwards only that task
// may call set_blue. The persistent buffer remains alive until TX completes.
esp_err_t board_led_init(board_led_t *led, int gpio);
esp_err_t board_led_set_blue(board_led_t *led, bool on, uint8_t brightness);
