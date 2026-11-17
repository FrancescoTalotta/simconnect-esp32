// SPDX-License-Identifier: LGPL-3.0-or-later
#include "app_settings.h"
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "nvs.h"

#if !CONFIG_APP_PUBLIC_FIRMWARE && __has_include("app_config.h")
#include "app_config.h"
#endif
#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID ""
#endif
#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD ""
#endif
#ifndef APP_SIM_HOST
#define APP_SIM_HOST "192.168.1.100"
#endif
#ifndef APP_SIM_PORT
#define APP_SIM_PORT "5111"
#endif
#ifndef APP_AIRCRAFT_MATCH
#define APP_AIRCRAFT_MATCH "FlyByWire"
#endif
#ifndef APP_RGB_LED_GPIO
#define APP_RGB_LED_GPIO 38
#endif
#ifndef APP_RGB_LED_BRIGHTNESS
#define APP_RGB_LED_BRIGHTNESS 16
#endif
_Static_assert(APP_RGB_LED_GPIO == 38 || APP_RGB_LED_GPIO == 48, "Use GPIO38 or GPIO48");
_Static_assert(APP_RGB_LED_BRIGHTNESS >= 0 && APP_RGB_LED_BRIGHTNESS <= 255,
               "Brightness must be 0..255");

static bool valid(const app_settings_t *s)
{
    struct in_addr address;
    if (!s->ssid[0] || !s->aircraft[0] || inet_pton(AF_INET, s->host, &address) != 1)
        return false;
    if (!s->port[0] || strspn(s->port, "0123456789") != strlen(s->port))
        return false;
    unsigned long port = strtoul(s->port, NULL, 10);
    size_t n = strlen(s->password);
    bool password_ok = n == 0 || (n >= 8 && n <= 63) ||
        (n == 64 && strspn(s->password, "0123456789abcdefABCDEF") == 64);
    return port > 0 && port <= 65535 && password_ok &&
        (s->rgb_gpio == 38 || s->rgb_gpio == 48);
}

bool app_settings_load(app_settings_t *settings)
{
    app_settings_t candidate = {
        .ssid = APP_WIFI_SSID, .password = APP_WIFI_PASSWORD,
        .host = APP_SIM_HOST, .port = APP_SIM_PORT, .aircraft = APP_AIRCRAFT_MATCH,
        .rgb_gpio = APP_RGB_LED_GPIO, .brightness = APP_RGB_LED_BRIGHTNESS,
    };
    nvs_handle_t handle;
    esp_err_t err = nvs_open("simconnect", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        // Require a complete settings set; never mix partial NVS with local secrets.
        size_t length;
#define READ_STRING(key, field) do { \
        length = sizeof(candidate.field); \
        err = nvs_get_str(handle, key, candidate.field, &length); \
        if (err != ESP_OK) goto close; \
    } while (0)
        READ_STRING("ssid", ssid);
        READ_STRING("password", password);
        READ_STRING("host", host);
        READ_STRING("port", port);
        READ_STRING("aircraft", aircraft);
#undef READ_STRING
        err = nvs_get_u8(handle, "rgb_gpio", &candidate.rgb_gpio);
        if (err == ESP_OK) err = nvs_get_u8(handle, "brightness", &candidate.brightness);
close:
        nvs_close(handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; // Local source builds retain the existing header workflow.
    }
    if (err != ESP_OK || !valid(&candidate)) {
        ESP_LOGE("settings", "Missing/invalid settings; flash a private NVS settings image "
                 "(see docs/flashing.md), or configure app_config.h for a source build");
        memset(&candidate, 0, sizeof(candidate));
        return false;
    }
    *settings = candidate;
    memset(&candidate, 0, sizeof(candidate));
    return true;
}
