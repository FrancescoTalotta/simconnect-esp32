// SPDX-License-Identifier: LGPL-3.0-or-later
#include <string.h>
#include "driver/gpio.h"
#include "board_led.h"

esp_err_t board_led_set_blue(board_led_t *led, bool on, uint8_t brightness)
{
    if (!led || !led->channel || !led->encoder) return ESP_ERR_INVALID_STATE;
    uint8_t grb[3];
    led_grb_bytes(0, 0, on ? brightness : 0, grb);
    unsigned index = 0;
    for (unsigned byte = 0; byte < 3; ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            unsigned high = led_bit_high_ticks(grb[byte], bit);
            led->symbols[index++] = (rmt_symbol_word_t) {
                .level0 = 1, .duration0 = high,
                .level1 = 0, .duration1 = LED_BIT_TICKS - high,
            };
        }
    }
    led->symbols[index] = (rmt_symbol_word_t) {
        .level0 = 0, .duration0 = LED_RESET_HALF_TICKS,
        .level1 = 0, .duration1 = LED_RESET_HALF_TICKS,
    };
    rmt_transmit_config_t transmit = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(led->channel, led->encoder, led->symbols,
                                  sizeof(led->symbols), &transmit);
    if (err != ESP_OK) return err;
    // A frame takes <0.4 ms. The caller aborts on error instead of reusing an
    // in-flight buffer after a timeout.
    return rmt_tx_wait_all_done(led->channel, 20);
}

esp_err_t board_led_init(board_led_t *led, int gpio)
{
    if (!led || !GPIO_IS_VALID_OUTPUT_GPIO(gpio)) return ESP_ERR_INVALID_ARG;
    memset(led, 0, sizeof(*led));
    rmt_tx_channel_config_t config = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    esp_err_t err = rmt_new_tx_channel(&config, &led->channel);
    if (err != ESP_OK) return err;
    rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &led->encoder);
    if (err != ESP_OK) goto cleanup_channel;
    err = rmt_enable(led->channel);
    if (err != ESP_OK) goto cleanup_encoder;
    err = board_led_set_blue(led, false, 0);
    if (err == ESP_OK) return ESP_OK;
    rmt_disable(led->channel);
cleanup_encoder:
    rmt_del_encoder(led->encoder);
    led->encoder = NULL;
cleanup_channel:
    rmt_del_channel(led->channel);
    led->channel = NULL;
    return err;
}
