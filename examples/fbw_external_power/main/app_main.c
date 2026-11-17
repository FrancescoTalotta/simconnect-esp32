// SPDX-License-Identifier: LGPL-3.0-or-later
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "simconnect_client.h"
#include "button_debounce.h"
#include "flight_gate.h"
#include "board_led.h"
#include "probe_indicator.h"
#include "app_settings.h"

#define WIFI_UP BIT0
#define BOOT_GPIO GPIO_NUM_0
#define READ_DEF 1
#define WRITE_DEF 2
#define HEAT_DEF 3
#define LIVE_REQUEST 1
#define HEAT_REQUEST 2
#define AIRCRAFT_LOADED_EVENT 100
#define SIM_STATE_EVENT 101

static const char *TAG = "fbw";
static app_settings_t settings;
static EventGroupHandle_t network;
static QueueHandle_t presses;
static QueueHandle_t probe_samples;
static board_led_t rgb_led;
static atomic_bool ready;
static atomic_bool led_is_blue;
static atomic_uint generation;
static atomic_uint indicator_epoch;
static atomic_uint wifi_generation;
static atomic_int application_state = FBW_WAIT_WIFI;
static const simc_item_t state_items[] = {
    { "TITLE", NULL, SIMC_STRING256, 0 },
    { "L:A32NX_OVHD_ELEC_EXT_PWR_PB_IS_ON", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_EXT_PWR_AVAIL:1", "number", SIMC_FLOAT64, 0 },
};
static const simc_item_t write_item =
    { "L:A32NX_OVHD_ELEC_EXT_PWR_PB_IS_ON", "number", SIMC_FLOAT64, 0 };
// Read-only in this example: drives a physical output, never written back.
// Full ON-legend emissive inputs, traced to A32NX v2024.1.0 cockpit XML.
static const simc_item_t probe_heat_items[] = {
    { "L:A32NX_MAN_PITOT_HEAT", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_ELEC_AC_2_BUS_IS_POWERED", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_ELEC_DC_2_BUS_IS_POWERED", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_OVHD_INTLT_ANN", "number", SIMC_FLOAT64, 0 },
};

typedef struct {
    unsigned generation;
    int64_t time_ms;
} press_t;

static int64_t milliseconds(void)
{
    return esp_timer_get_time() / 1000;
}

static bool wifi_up(void)
{
    return (xEventGroupGetBits(network) & WIFI_UP) != 0;
}

static void invalidate(void)
{
    atomic_store(&ready, false);
    atomic_fetch_add(&generation, 1);
}

// Session-owner only. Invalidate cached LED data across loading/aircraft changes.
static void invalidate_indicator(void)
{
    probe_sample_t invalid = {0};
    atomic_fetch_add(&indicator_epoch, 1);
    xQueueOverwrite(probe_samples, &invalid);
}

// Called only by the session owner; the Wi-Fi callback just invalidates controls.
static void set_state(fbw_state_t state, const char *reason)
{
    fbw_state_t previous = atomic_load(&application_state);
    if (state != previous) {
        if (state != FBW_READY) {
            invalidate();
            xQueueReset(presses);
        }
        if (state != FBW_READY && state != FBW_CONFIRMING) invalidate_indicator();
        atomic_store(&application_state, state);
        ESP_LOGI(TAG, "State %s: %s", fbw_state_name(state), reason);
    }
    atomic_store(&ready, state == FBW_READY);
}

// Owns all RGB writes. Checking age and Wi-Fi here keeps stale data from leaving
// the LED on while the session owner is blocked in a network operation.
static void indicator_task(void *arg)
{
    (void)arg;
    probe_sample_t latest = {0};
    uint8_t previous = 0;
    ESP_LOGI(TAG, "RGB LED GPIO%d: off; blue follows PROBE/WINDOW HEAT ON legend",
             settings.rgb_gpio);
    for (;;) {
        probe_sample_t update;
        if (xQueueReceive(probe_samples, &update, 0) == pdTRUE) latest = update;
        bool blue = settings.brightness > 0 && probe_indicator_should_light(
            &latest, atomic_load(&application_state), wifi_up(),
            atomic_load(&indicator_epoch), atomic_load(&wifi_generation), milliseconds());
        uint8_t brightness = blue ?
            probe_indicator_brightness(&latest, settings.brightness) : 0;
        if (brightness != previous) {
            ESP_ERROR_CHECK(board_led_set_blue(&rgb_led, blue, brightness));
            atomic_store(&led_is_blue, blue);
            ESP_LOGI(TAG, "RGB LED=%s brightness=%u", blue ? "BLUE (heat ON legend)" :
                     "off (legend dark or data not usable)", (unsigned)brightness);
            previous = brightness;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Wi-Fi IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(network, WIFI_UP);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        xEventGroupClearBits(network, WIFI_UP);
        invalidate();
        atomic_fetch_add(&wifi_generation, 1);
        ESP_LOGW(TAG, "Wi-Fi lost, reason=%u; actions discarded", event->reason);
    }
}

static void wifi_retry(void *arg)
{
    (void)arg;
    if (!wifi_up()) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) ESP_LOGD(TAG, "Wi-Fi retry: %s", esp_err_to_name(err));
    }
}

static void start_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta()) abort();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t config = {0};
    size_t ssid_len = strlen(settings.ssid);
    size_t password_len = strlen(settings.password);
    if (!ssid_len || ssid_len > sizeof(config.sta.ssid) ||
        password_len > sizeof(config.sta.password)) {
        ESP_LOGE(TAG, "Invalid Wi-Fi settings");
        return;
    }
    memcpy(config.sta.ssid, settings.ssid, ssid_len);
    memcpy(config.sta.password, settings.password, password_len);
    config.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_connect());
    esp_timer_handle_t retry;
    esp_timer_create_args_t timer = { .callback = wifi_retry, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&timer, &retry));
    ESP_ERROR_CHECK(esp_timer_start_periodic(retry, 5000000));
}

static void button_task(void *arg)
{
    (void)arg;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    button_debounce_t debounce;
    button_debounce_init(&debounce, gpio_get_level(BOOT_GPIO), milliseconds());
    for (;;) {
        int level = gpio_get_level(BOOT_GPIO);
        int64_t now = milliseconds();
        if (button_debounce_update(&debounce, level, now)) {
            unsigned press_generation = atomic_load(&generation);
            if (atomic_load(&ready)) {
                press_t press = { press_generation, now };
                if (xQueueSend(presses, &press, 0) != pdTRUE)
                    ESP_LOGW(TAG, "Button queue full; press ignored");
            } else {
                ESP_LOGW(TAG, "BOOT ignored: %s",
                         fbw_state_name(atomic_load(&application_state)));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool decode_snapshot(const simc_message_t *message, int *on, int *available,
                            char title[256], bool report)
{
    simc_value_t values[3];
    int r = simc_decode_data(message, state_items, 3, values);
    if (r != SIMC_OK) {
        if (report) {
            ESP_LOGW(TAG, "State layout mismatch: req=%" PRIu32 " def=%" PRIu32
                     " object=%" PRIu32 " flags=0x%" PRIx32
                     " count=%" PRIu32 " bytes=%zu; expected count=3 bytes=272. "
                     "Controls disabled; keeping connection.",
                     message->request_id, message->definition_id, message->object_id,
                     message->flags, message->count, message->data_size);
        }
        return false;
    }
    memcpy(title, values[0].string, 256);
    if (!settings.aircraft[0] || !strstr(title, settings.aircraft)) {
        if (report) ESP_LOGI(TAG, "Waiting for aircraft: TITLE='%s', required substring='%s'",
                            title, settings.aircraft);
        return false;
    }
    // SimConnect booleans may use either 1 or -1 for true.
    if (!flight_bool_valid(values[1].number) || !flight_bool_valid(values[2].number)) {
        if (report) ESP_LOGW(TAG, "Waiting for valid LVars: selection=%g availability=%g",
                            values[1].number, values[2].number);
        return false;
    }
    *on = values[1].number != 0;
    *available = values[2].number != 0;
    return true;
}

static void cancel_action(flight_requests_t *requests)
{
    flight_requests_cancel(requests);
    invalidate_indicator();
    invalidate();
    xQueueReset(presses);
}

// Receive one coherent snapshot of all four annunciator inputs.
static bool handle_probe_heat(const simc_message_t *message, const flight_gate_t *gate,
                              uint32_t network_epoch, int64_t now, bool report)
{
    simc_value_t values[4];
    int r = simc_decode_data(message, probe_heat_items, 4, values);
    if (r != SIMC_OK || !flight_bool_valid(values[0].number) ||
        !flight_bool_valid(values[1].number) || !flight_bool_valid(values[2].number) ||
        !(values[3].number == 0 || values[3].number == 1 || values[3].number == 2) ||
        message->object_id != gate->object_id) {
        invalidate_indicator();
        if (report) {
            ESP_LOGW(TAG, "Probe heat data rejected: object=%" PRIu32
                     " expected_object=%" PRIu32 " flags=0x%" PRIx32
                     " count=%" PRIu32 " bytes=%zu; expected count=4 bytes=32",
                     message->object_id, gate->object_id, message->flags,
                     message->count, message->data_size);
        }
        return false;
    }
    probe_sample_t sample = {
        .valid = true,
        .manual_on = values[0].number != 0,
        .ac2_powered = values[1].number != 0,
        .dc2_powered = values[2].number != 0,
        .ann_mode = (unsigned)values[3].number,
        .epoch = atomic_load(&indicator_epoch),
        .network_epoch = network_epoch,
        .received_ms = now,
    };
    ESP_LOGD(TAG, "Heat legend: manual=%d AC2=%d DC2=%d ANN=%u",
             sample.manual_on, sample.ac2_powered, sample.dc2_powered, sample.ann_mode);
    // One-slot queue: only the latest reading matters; the LED task owns RMT.
    xQueueOverwrite(probe_samples, &sample);
    return true;
}

// Only this task owns the SimConnect session. No socket access from button/console.
static int run_session(simc_session_t *session)
{
    int r = simc_add_definition(session, READ_DEF, state_items, 3);
    if (r != SIMC_OK) return r;
    r = simc_add_definition(session, WRITE_DEF, &write_item, 1);
    if (r != SIMC_OK) return r;
    r = simc_add_definition(session, HEAT_DEF, probe_heat_items, 4);
    if (r != SIMC_OK) return r;
    r = simc_subscribe_system_event(session, AIRCRAFT_LOADED_EVENT, "AircraftLoaded");
    if (r != SIMC_OK) return r;
    // "Sim" immediately returns the current running state, then reports changes.
    r = simc_subscribe_system_event(session, SIM_STATE_EVENT, "Sim");
    if (r != SIMC_OK) return r;
    // Not CHANGED: absence of snapshots disables controls, but does not close TCP.
    r = simc_request_data(session, LIVE_REQUEST, READ_DEF, SIMC_PERIOD_SECOND, 0);
    if (r != SIMC_OK) return r;
    // Full periodic values, not CHANGED, so a constant ON state stays fresh.
    r = simc_request_data(session, HEAT_REQUEST, HEAT_DEF, SIMC_PERIOD_SECOND, 0);
    if (r != SIMC_OK) return r;
    enum { IDLE, FRESH, CONFIRM } phase = IDLE;
    flight_gate_t gate = {0};
    flight_requests_t requests = { .next = 3 }; // 1=state, 2=heat subscription.
    int desired = 0, last_on = -1, last_available = -1;
    int64_t action_deadline = 0, next_confirm = 0, last_diagnostic = -10000;
    int64_t last_heat_diagnostic = -10000;
    unsigned network_epoch = atomic_load(&wifi_generation);
    set_state(FBW_WAIT_FLIGHT, "connected; waiting for a running flight");
    for (;;) {
        int64_t now = milliseconds();
        if (!wifi_up() || network_epoch != atomic_load(&wifi_generation)) return SIMC_IO;
        fbw_state_t gate_state = flight_gate_state(&gate, now);
        if (phase != IDLE && (gate_state != FBW_READY || now >= action_deadline)) {
            ESP_LOGW(TAG, "Pending action expired or flight not ready; discarded, NOT replaying");
            cancel_action(&requests);
            phase = IDLE;
            flight_gate_reset_aircraft(&gate);
            gate_state = flight_gate_state(&gate, now);
        }
        if (phase == IDLE) {
            set_state(gate_state, gate_state == FBW_WAIT_FLIGHT ? "waiting for running flight" :
                      gate_state == FBW_WAIT_AIRCRAFT ? "waiting for two stable, valid aircraft snapshots" :
                      "aircraft verified; BOOT enabled");
        }
        if (phase == CONFIRM && !requests.confirm && now >= next_confirm) {
            requests.confirm = flight_request_allocate(&requests);
            if (!requests.confirm) return SIMC_PROTOCOL_ERROR;
            r = simc_request_data(session, requests.confirm, READ_DEF, SIMC_PERIOD_ONCE, 0);
            if (r != SIMC_OK) return r;
        }
        press_t press;
        if (xQueueReceive(presses, &press, 0) == pdTRUE) {
            if (phase == IDLE && gate_state == FBW_READY &&
                press.generation == atomic_load(&generation) &&
                now - press.time_ms <= 300) {
                set_state(FBW_CONFIRMING, "refreshing aircraft state before action");
                requests.fresh = flight_request_allocate(&requests);
                if (!requests.fresh) return SIMC_PROTOCOL_ERROR;
                r = simc_request_data(session, requests.fresh, READ_DEF, SIMC_PERIOD_ONCE, 0);
                if (r != SIMC_OK) return r;
                phase = FRESH;
                action_deadline = now + 3000;
            }
        }
        simc_message_t message;
        r = simc_poll(session, 50, &message);
        if (r == SIMC_TIMEOUT) continue;
        if (r != SIMC_OK) return r;
        // Wi-Fi may have dropped while waiting for the packet.
        if (!wifi_up() || network_epoch != atomic_load(&wifi_generation)) return SIMC_IO;
        if (phase != IDLE && (milliseconds() >= action_deadline ||
                             flight_gate_state(&gate, milliseconds()) != FBW_READY)) {
            ESP_LOGW(TAG, "Late action response discarded; NOT replaying");
            cancel_action(&requests);
            phase = IDLE;
            flight_gate_reset_aircraft(&gate);
        }
        if (message.type == SIMC_MESSAGE_EXCEPTION) {
            ESP_LOGE(TAG, "SimConnect exception=%" PRIu32 " send=%" PRIu32 " index=%" PRIu32,
                     message.exception, message.send_id, message.index);
            return SIMC_EXCEPTION;
        }
        if (message.type == SIMC_MESSAGE_EVENT) {
            bool changed = false;
            if (message.event_id == SIM_STATE_EVENT) {
                changed = flight_gate_sim(&gate, message.event_data != 0);
            } else if (message.event_id == AIRCRAFT_LOADED_EVENT) {
                ESP_LOGI(TAG, "AircraftLoaded: clearing readiness, keeping SimConnect open");
                flight_gate_reset_aircraft(&gate);
                changed = true;
            }
            if (changed) {
                cancel_action(&requests);
                phase = IDLE;
                set_state(flight_gate_state(&gate, milliseconds()),
                          "flight/aircraft changed; waiting for fresh verification");
            }
            continue;
        }
        if (message.type == SIMC_MESSAGE_DATA && message.definition_id == HEAT_DEF &&
            message.request_id == HEAT_REQUEST) {
            now = milliseconds();
            if (flight_gate_state(&gate, now) != FBW_READY) {
                invalidate_indicator();
                continue;
            }
            bool report = now - last_heat_diagnostic >= 10000;
            if (!handle_probe_heat(&message, &gate, network_epoch, now, report) && report)
                last_heat_diagnostic = now;
            continue;
        }
        if (message.type != SIMC_MESSAGE_DATA || message.definition_id != READ_DEF ||
            (message.request_id != LIVE_REQUEST &&
             !(requests.fresh && message.request_id == requests.fresh) &&
             !(requests.confirm && message.request_id == requests.confirm))) continue;
        if (!gate.sim_known || !gate.sim_running) continue;
        int on, available;
        char title[256];
        now = milliseconds();
        if (!decode_snapshot(&message, &on, &available, title, now - last_diagnostic >= 10000)) {
            if (now - last_diagnostic >= 10000) last_diagnostic = now;
            cancel_action(&requests);
            phase = IDLE;
            flight_gate_reset_aircraft(&gate);
            set_state(FBW_WAIT_AIRCRAFT, "state data not usable yet; controls disabled");
            continue;
        }
        bool same_aircraft = flight_gate_identity(&gate, title, message.object_id);
        if (phase != IDLE && !same_aircraft) {
            ESP_LOGW(TAG, "Aircraft title/object changed during action; discarded");
            cancel_action(&requests);
            phase = IDLE;
            flight_gate_reset_aircraft(&gate);
        }
        if (message.request_id == LIVE_REQUEST) {
            if (!same_aircraft) ESP_LOGI(TAG, "Aircraft candidate: '%s' object=%" PRIu32,
                                        title, message.object_id);
            flight_gate_observe(&gate, title, message.object_id, now);
        }
        if (on != last_on || available != last_available) {
            ESP_LOGI(TAG, "EXT PWR selection=%s availability=%s",
                     on ? "ON" : "OFF", available ? "AVAIL" : "unavailable");
            last_on = on;
            last_available = available;
        }
        if (phase == FRESH && message.request_id == requests.fresh) {
            requests.fresh = 0;
            desired = !on;
            if (desired && !available) {
                ESP_LOGW(TAG, "External power unavailable; ON request ignored");
                phase = IDLE;
            } else {
                uint32_t serial;
                r = simc_set_double(session, WRITE_DEF, desired, &serial);
                if (r != SIMC_OK) return r;
                ESP_LOGI(TAG, "EXT PWR %s sent (send=%" PRIu32 "); awaiting observed state",
                         desired ? "ON" : "OFF", serial);
                phase = CONFIRM;
                requests.confirm = 0;
                next_confirm = milliseconds() + 200;
                action_deadline = milliseconds() + 3000;
            }
        } else if (phase == CONFIRM && message.request_id == requests.confirm) {
            requests.confirm = 0;
            if (on == desired) {
                ESP_LOGI(TAG, "EXT PWR %s confirmed by simulator", on ? "ON" : "OFF");
                phase = IDLE;
            } else {
                next_confirm = milliseconds() + 200;
            }
        }
    }
}

static void sim_task(void *arg)
{
    (void)arg;
    unsigned backoff = 1000;
    ESP_LOGI(TAG, "State WAIT_WIFI: waiting for network address");
    for (;;) {
        invalidate();
        xQueueReset(presses);
        if (!wifi_up()) set_state(FBW_WAIT_WIFI, "waiting for network address");
        xEventGroupWaitBits(network, WIFI_UP, pdFALSE, pdTRUE, portMAX_DELAY);
        set_state(FBW_WAIT_SIMULATOR, "waiting for SimConnect");
        simc_config_t config = {
            .host = settings.host, .port = settings.port,
            .client_name = "ESP32-S3 FBW external power",
            .protocol = SIMC_PROTOCOL_MSFS2020, .timeout_ms = 3000,
        };
        simc_session_t *session = NULL;
        ESP_LOGI(TAG, "Connecting to SimConnect %s:%s", config.host, config.port);
        int r = simc_open(&session, &config);
        if (r == SIMC_OK) {
            ESP_LOGI(TAG, "SimConnect OPEN received");
            int64_t opened = milliseconds();
            r = run_session(session);
            if (milliseconds() - opened >= 10000) backoff = 1000;
        }
        invalidate();
        simc_close(session);
        set_state(wifi_up() ? FBW_WAIT_SIMULATOR : FBW_WAIT_WIFI,
                  "connection closed; pending actions discarded");
        ESP_LOGW(TAG, "Session ended: %s; retry in %u ms", simc_error_name(r), backoff);
        vTaskDelay(pdMS_TO_TICKS(backoff));
        if (backoff < 8000) backoff *= 2;
    }
}

static int status_command(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("uptime=%" PRId64 "s reset=%d wifi=%s state=%s controls=%s probe_led=%s target=esp32s3\n",
           milliseconds() / 1000, (int)esp_reset_reason(), wifi_up() ? "up" : "down",
           fbw_state_name(wifi_up() ? atomic_load(&application_state) : FBW_WAIT_WIFI),
           atomic_load(&ready) ? "ready" : "not ready",
           atomic_load(&led_is_blue) ? "BLUE" : "off");
    return 0;
}

static int heap_command(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("heap free=%zu min=%zu largest=%zu\n",
           heap_caps_get_free_size(MALLOC_CAP_8BIT),
           heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return 0;
}

static int settings_command(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("sim=%s:%s aircraft_match=%s BOOT=GPIO0 active-low RGB=GPIO%d brightness=%d\n"
           "Wi-Fi credentials hidden. See docs/flashing.md for NVS configuration.\n",
           settings.host, settings.port, settings.aircraft,
           settings.rgb_gpio, settings.brightness);
    return 0;
}

static int tasks_command(int argc, char **argv)
{
    (void)argc; (void)argv;
    UBaseType_t count = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *tasks = calloc(count, sizeof(*tasks));
    if (!tasks) return 1;
    count = uxTaskGetSystemState(tasks, count, NULL);
    if (!count) printf("Task list changed; try again\n");
    for (UBaseType_t i = 0; i < count; ++i)
        printf("%-16s priority=%u stack_high_water=%u\n", tasks[i].pcTaskName,
               (unsigned)tasks[i].uxCurrentPriority, (unsigned)tasks[i].usStackHighWaterMark);
    free(tasks);
    return 0;
}

static int log_command(int argc, char **argv)
{
    if (argc != 3 || strlen(argv[2]) != 1 || argv[2][0] < '0' || argv[2][0] > '5') {
        printf("usage: log <tag|*> <0..5> (0=none, 3=info, 5=verbose)\n");
        return 1;
    }
    esp_log_level_set(argv[1], (esp_log_level_t)(argv[2][0] - '0'));
    return 0;
}

static void start_console(void)
{
    const esp_console_cmd_t commands[] = {
        { .command = "status", .help = "Connectivity, readiness and uptime", .func = status_command },
        { .command = "heap", .help = "Heap diagnostics", .func = heap_command },
        { .command = "settings", .help = "Show nonsecret test configuration", .func = settings_command },
        { .command = "tasks", .help = "Task priorities and stack margins", .func = tasks_command },
        { .command = "log", .help = "Set log level: log <tag|*> <0..5>", .func = log_command },
    };
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "simconnect> ";
    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart, &config, &repl));
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void)
{
    ESP_LOGI(TAG, "FBW A32NX MSFS2024 test; BOOT GPIO0; LGPL-3.0-or-later");
    esp_err_t err = nvs_flash_init();
    // Do not silently erase an existing device's NVS.
    ESP_ERROR_CHECK(err);
    network = xEventGroupCreate();
    presses = xQueueCreate(4, sizeof(press_t));
    probe_samples = xQueueCreate(1, sizeof(probe_sample_t));
    if (!network || !presses || !probe_samples) abort();
    if (!app_settings_load(&settings)) {
        // No networking, control tasks, or RGB output until configuration is valid.
        start_console();
        return;
    }
    ESP_ERROR_CHECK(board_led_init(&rgb_led, settings.rgb_gpio));
    if (xTaskCreate(indicator_task, "probe_led", 3072, NULL, 3, NULL) != pdPASS) abort();
    esp_log_level_set("wifi", ESP_LOG_WARN);
    start_wifi();
    if (xTaskCreate(button_task, "boot_button", 3072, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(sim_task, "simconnect", 8192, NULL, 4, NULL) != pdPASS) abort();
    start_console();
}
