// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char ssid[33];
    char password[65];
    char host[16];
    char port[6];
    char aircraft[256];
    uint8_t rgb_gpio;
    uint8_t brightness;
} app_settings_t;

// Called once before creating tasks; settings remain immutable afterwards.
bool app_settings_load(app_settings_t *settings);
