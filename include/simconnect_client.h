// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single-owner API: all operations, including close, must run on the same task.
 * Initialize the network before opening. No background task or automatic retry.
 * Successful writes mean "sent", NOT "applied"; poll continuously for exceptions.
 * Unlike the earlier local prototype, this API uses one persistent session.
 */
typedef struct simc_session simc_session_t;

enum {
    SIMC_OK = 0,
    SIMC_TIMEOUT = -1,      // Poll only: nonfatal; partial receive is retained.
    SIMC_IO = -2,           // Fatal: close and reopen; never replay uncertain writes.
    SIMC_PROTOCOL_ERROR = -3,
    SIMC_INVALID_ARG = -4,
    SIMC_NO_MEMORY = -5,
    SIMC_QUIT = -6,
    SIMC_EXCEPTION = -7,     // OPEN rejected (later exceptions arrive as messages).
};
enum {
    SIMC_PROTOCOL_FSX_SP2 = 4,
    SIMC_PROTOCOL_MSFS2020 = 5,
    SIMC_PROTOCOL_MSFS2024 = 6,
    SIMC_FLOAT64 = 4,
    SIMC_STRING256 = 9,
    SIMC_PERIOD_NEVER = 0,
    SIMC_PERIOD_ONCE = 1,
    SIMC_PERIOD_VISUAL_FRAME = 2,
    SIMC_PERIOD_SIM_FRAME = 3,
    SIMC_PERIOD_SECOND = 4,
    SIMC_CHANGED = 1,
    SIMC_MAX_ITEMS = 16,
    SIMC_MAX_FRAME = 8192,
};

typedef struct {
    const char *host;        // Numeric IPv4 address; no unbounded DNS lookup.
    const char *port;        // Decimal TCP port, 1..65535.
    const char *client_name; // 1..255 bytes.
    uint32_t protocol;       // Use SIMC_PROTOCOL_MSFS2020 for MSFS 2020/2024.
    uint32_t timeout_ms;     // 1..60000; connect/send/OPEN/partial-frame deadline.
} simc_config_t;

typedef struct {
    const char *name;
    const char *units;       // NULL for STRING256; "number" for LVars.
    uint32_t type;           // FLOAT64 or STRING256 only.
    float epsilon;          // Server-side CHANGED threshold.
} simc_item_t;

typedef union {
    double number;
    char string[256];        // Always NUL-terminated by simc_decode_data.
} simc_value_t;

typedef enum {
    SIMC_MESSAGE_OTHER,
    SIMC_MESSAGE_DATA,
    SIMC_MESSAGE_EXCEPTION,
    SIMC_MESSAGE_EVENT,
} simc_message_type_t;

typedef struct {
    simc_message_type_t type;
    uint32_t id;             // Wire receive ID.
    uint32_t request_id;
    uint32_t definition_id;
    uint32_t object_id;
    uint32_t flags;
    uint32_t count;
    uint32_t exception;
    uint32_t send_id;        // Exception correlation.
    uint32_t index;          // Exception parameter index.
    uint32_t event_id;
    uint32_t event_data;
    const uint8_t *data;     // Borrowed until next poll/close; copy if needed.
    size_t data_size;
} simc_message_t;

int simc_open(simc_session_t **out, const simc_config_t *config);
void simc_close(simc_session_t *session);
int simc_add_definition(simc_session_t *session, uint32_t definition_id,
                        const simc_item_t *items, size_t count);
int simc_clear_definition(simc_session_t *session, uint32_t definition_id);
int simc_request_data(simc_session_t *session, uint32_t request_id,
                      uint32_t definition_id, uint32_t period, uint32_t flags);
// Define exactly one FLOAT64 in this definition before using set_double.
int simc_set_double(simc_session_t *session, uint32_t definition_id,
                    double value, uint32_t *send_id);
int simc_map_event(simc_session_t *session, uint32_t event_id, const char *name);
int simc_send_event(simc_session_t *session, uint32_t event_id,
                    const uint32_t data[5], uint32_t *send_id);
int simc_subscribe_system_event(simc_session_t *session, uint32_t event_id,
                                const char *name);
int simc_poll(simc_session_t *session, uint32_t timeout_ms, simc_message_t *out);
// Validates count, flags, payload size, and datatype layout. The caller matches
// request/definition IDs; a user-aircraft response may have a nonzero object ID.
int simc_decode_data(const simc_message_t *message, const simc_item_t *items,
                      size_t count, simc_value_t *out);
const char *simc_error_name(int error);

#ifdef __cplusplus
}
#endif
