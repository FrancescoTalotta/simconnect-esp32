// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Adapted from the earlier local ESP-IDF simconnect_client.c prototype.
 * Wire layout reference: EvenAR/node-simconnect (LGPL-3.0-or-later),
 * by Even Arneberg Rognlien and contributors. See NOTICE.md.
 * Changes: persistent API, deadlines, receive retention, validation, dispatch,
 * STRING256 identity data, explicit event parameters, portable host tests.
 */
#include "simconnect_client.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

_Static_assert(sizeof(double) == 8 && sizeof(float) == 4, "SimConnect requires IEEE float sizes");

struct simc_session {
    int sock;
    uint32_t protocol;
    uint32_t next_send_id;
    uint32_t timeout_ms;
    size_t used;
    size_t needed;
    int64_t partial_deadline;
    uint8_t rx[SIMC_MAX_FRAME];
};

static int64_t now_ms(void)
{
#ifdef ESP_PLATFORM
    return esp_timer_get_time() / 1000;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void put_double(uint8_t *p, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, 8);
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(bits >> (8 * i));
}

static double get_double(const uint8_t *p)
{
    uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i) bits |= (uint64_t)p[i] << (8 * i);
    double value;
    memcpy(&value, &bits, 8);
    return value;
}

static bool valid_string(const char *s)
{
    return s && s[0] && strnlen(s, 256) < 256;
}

static int fail(simc_session_t *s, int error)
{
    if (s->sock >= 0) close(s->sock);
    s->sock = -1;
    return error;
}

static int wait_socket(int fd, bool writing, int64_t deadline)
{
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) return SIMC_TIMEOUT;
        struct timeval tv = { .tv_sec = (long)(left / 1000),
                              .tv_usec = (long)(left % 1000) * 1000 };
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        int r = select(fd + 1, writing ? NULL : &fds, writing ? &fds : NULL, NULL, &tv);
        if (r > 0) return SIMC_OK;
        if (r == 0) return SIMC_TIMEOUT;
        if (errno != EINTR) return SIMC_IO;
    }
}

static int send_bytes(simc_session_t *s, const uint8_t *p, size_t size, int64_t deadline)
{
    while (size) {
        if (wait_socket(s->sock, true, deadline) != SIMC_OK) return fail(s, SIMC_IO);
#ifdef MSG_NOSIGNAL
        ssize_t n = send(s->sock, p, size, MSG_NOSIGNAL);
#else
        ssize_t n = send(s->sock, p, size, 0);
#endif
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return fail(s, SIMC_IO);
        size -= (size_t)n;
        p += n;
    }
    return SIMC_OK;
}

static int packet(simc_session_t *s, uint32_t id, const uint8_t *payload,
                  size_t size, uint32_t *send_id)
{
    if (!s || s->sock < 0) return SIMC_IO;
    uint8_t header[16];
    uint32_t serial = s->next_send_id++;
    put32(header, (uint32_t)(16 + size));
    put32(header + 4, s->protocol);
    put32(header + 8, id | 0xf0000000u);
    put32(header + 12, serial);
    int64_t deadline = now_ms() + s->timeout_ms;
    int r = send_bytes(s, header, sizeof(header), deadline);
    if (r == SIMC_OK) r = send_bytes(s, payload, size, deadline);
    if (r == SIMC_OK && send_id) *send_id = serial;
    return r;
}

// Read only the current frame. A poll timeout does not discard partial bytes.
static int receive(simc_session_t *s, uint32_t timeout_ms, size_t *size)
{
    int64_t deadline = now_ms() + timeout_ms;
    if (s->needed == 0) s->needed = 12;
    while (s->used < s->needed) {
        int64_t limit = deadline;
        if (s->used && s->partial_deadline < limit) limit = s->partial_deadline;
        int r = wait_socket(s->sock, false, limit);
        if (r == SIMC_TIMEOUT) {
            if (s->used && now_ms() >= s->partial_deadline)
                return fail(s, SIMC_PROTOCOL_ERROR);
            return SIMC_TIMEOUT;
        }
        if (r != SIMC_OK) return fail(s, r);
        ssize_t n = recv(s->sock, s->rx + s->used, s->needed - s->used, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return fail(s, SIMC_IO);
        if (s->used == 0) s->partial_deadline = now_ms() + s->timeout_ms;
        s->used += (size_t)n;
        if (s->used == 12) {
            uint32_t length = get32(s->rx);
            if (length < 12 || length > SIMC_MAX_FRAME)
                return fail(s, SIMC_PROTOCOL_ERROR);
            s->needed = length;
        }
    }
    *size = s->needed;
    s->used = s->needed = 0;
    return SIMC_OK;
}

int simc_poll(simc_session_t *s, uint32_t timeout_ms, simc_message_t *out)
{
    if (!s || !out || timeout_ms == 0 || timeout_ms > 60000) return SIMC_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (s->sock < 0) return SIMC_IO;
    size_t size;
    int r = receive(s, timeout_ms, &size);
    if (r != SIMC_OK) return r;
    const uint8_t *b = s->rx;
    out->id = get32(b + 8);
    switch (out->id) {
    case 1: // EXCEPTION, not OBJECT_ADDREMOVE (5).
        if (size != 24) return fail(s, SIMC_PROTOCOL_ERROR);
        out->type = SIMC_MESSAGE_EXCEPTION;
        out->exception = get32(b + 12);
        out->send_id = get32(b + 16);
        out->index = get32(b + 20);
        break;
    case 2: // OPEN: fixed name plus version/build/reserved fields.
        if (size < 308) return fail(s, SIMC_PROTOCOL_ERROR);
        break;
    case 3:
        return fail(s, SIMC_QUIT);
    case 4: // EVENT
        if (size < 24) return fail(s, SIMC_PROTOCOL_ERROR);
        out->type = SIMC_MESSAGE_EVENT;
        out->event_id = get32(b + 16);
        out->event_data = get32(b + 20);
        break;
    case 6: // EVENT_FILENAME (AircraftLoaded)
        if (size < 284) return fail(s, SIMC_PROTOCOL_ERROR);
        out->type = SIMC_MESSAGE_EVENT;
        out->event_id = get32(b + 16);
        break;
    case 8: // SIMOBJECT_DATA
        if (size < 40) return fail(s, SIMC_PROTOCOL_ERROR);
        out->type = SIMC_MESSAGE_DATA;
        out->request_id = get32(b + 12);
        out->object_id = get32(b + 16);
        out->definition_id = get32(b + 20);
        out->flags = get32(b + 24);
        out->count = get32(b + 36);
        out->data = b + 40;
        out->data_size = size - 40;
        break;
    default:
        break; // Unknown, but bounded, frame is safely skipped.
    }
    return SIMC_OK;
}

int simc_open(simc_session_t **out, const simc_config_t *config)
{
    if (!out) return SIMC_INVALID_ARG;
    *out = NULL;
    if (!config || !valid_string(config->host) || !valid_string(config->port) ||
        !valid_string(config->client_name) || config->timeout_ms == 0 ||
        config->timeout_ms > 60000 || config->protocol < 4 || config->protocol > 6)
        return SIMC_INVALID_ARG;
    for (const char *p = config->port; *p; ++p)
        if (*p < '0' || *p > '9') return SIMC_INVALID_ARG;
    char *end;
    errno = 0;
    long port = strtol(config->port, &end, 10);
    if (errno || *end || port < 1 || port > 65535) return SIMC_INVALID_ARG;
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    if (inet_pton(AF_INET, config->host, &address.sin_addr) != 1) return SIMC_INVALID_ARG;

    simc_session_t *s = calloc(1, sizeof(*s));
    if (!s) return SIMC_NO_MEMORY;
    s->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    s->protocol = config->protocol;
    s->timeout_ms = config->timeout_ms;
    if (s->sock < 0 || s->sock >= FD_SETSIZE) goto io_error;
    int flags = fcntl(s->sock, F_GETFL, 0);
    if (flags < 0 || fcntl(s->sock, F_SETFL, flags | O_NONBLOCK) < 0) goto io_error;
    int yes = 1;
    if (setsockopt(s->sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) goto io_error;
    // Loading may legitimately stop application data. Let TCP, not the aircraft
    // snapshot timer, detect a vanished PC or broken route.
    if (setsockopt(s->sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) < 0) goto io_error;
#if defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE)
    int idle_seconds = 20;
#ifdef TCP_KEEPIDLE
    if (setsockopt(s->sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle_seconds, sizeof(idle_seconds)) < 0)
        goto io_error;
#else
    if (setsockopt(s->sock, IPPROTO_TCP, TCP_KEEPALIVE, &idle_seconds, sizeof(idle_seconds)) < 0)
        goto io_error;
#endif
#endif
#ifdef TCP_KEEPINTVL
    int interval_seconds = 5;
    if (setsockopt(s->sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval_seconds, sizeof(interval_seconds)) < 0)
        goto io_error;
#endif
#ifdef TCP_KEEPCNT
    int keepalive_count = 3;
    if (setsockopt(s->sock, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_count, sizeof(keepalive_count)) < 0)
        goto io_error;
#endif
#ifdef SO_NOSIGPIPE
    if (setsockopt(s->sock, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) < 0) goto io_error;
#endif
    int r = connect(s->sock, (struct sockaddr *)&address, sizeof(address));
    if (r < 0) {
        if (errno != EINPROGRESS) goto io_error;
        if (wait_socket(s->sock, true, now_ms() + s->timeout_ms) != SIMC_OK) goto io_error;
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(s->sock, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) goto io_error;
    }
    uint8_t payload[280] = {0};
    memcpy(payload, config->client_name, strlen(config->client_name));
    const char *alias = config->protocol == 4 ? "XSF" : config->protocol == 5 ? "HK" : "RS";
    memcpy(payload + 261, alias, strlen(alias));
    put32(payload + 264, config->protocol == 4 ? 10 : config->protocol == 5 ? 11 : 12);
    put32(payload + 268, config->protocol == 6 ? 2 : 0);
    put32(payload + 272, config->protocol == 4 ? 61259 : config->protocol == 5 ? 62651 : 282174);
    put32(payload + 276, config->protocol == 4 ? 0 : config->protocol == 5 ? 3 : 999);
    r = packet(s, 1, payload, sizeof(payload), NULL);
    if (r != SIMC_OK) goto io_error;
    int64_t deadline = now_ms() + s->timeout_ms;
    while (now_ms() < deadline) {
        simc_message_t message;
        uint32_t left = (uint32_t)(deadline - now_ms());
        if (!left || left > s->timeout_ms) break;
        r = simc_poll(s, left, &message);
        if (r == SIMC_OK && message.id == 2) {
            *out = s;
            return SIMC_OK;
        }
        if (r == SIMC_OK && message.type == SIMC_MESSAGE_EXCEPTION) {
            simc_close(s);
            return SIMC_EXCEPTION;
        }
        if (r != SIMC_OK) {
            simc_close(s);
            return r;
        }
    }
io_error:
    simc_close(s);
    return SIMC_IO;
}

void simc_close(simc_session_t *s)
{
    if (!s) return;
    if (s->sock >= 0) close(s->sock);
    free(s);
}

static bool valid_items(const simc_item_t *items, size_t count)
{
    if (!items || !count || count > SIMC_MAX_ITEMS) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!valid_string(items[i].name) ||
            (items[i].units && !valid_string(items[i].units)) ||
            (items[i].type != SIMC_FLOAT64 && items[i].type != SIMC_STRING256) ||
            !isfinite(items[i].epsilon) || items[i].epsilon < 0) return false;
    }
    return true;
}

int simc_add_definition(simc_session_t *s, uint32_t id,
                        const simc_item_t *items, size_t count)
{
    if (!valid_items(items, count)) return SIMC_INVALID_ARG;
    for (size_t i = 0; i < count; ++i) {
        uint8_t payload[528] = {0};
        put32(payload, id);
        memcpy(payload + 4, items[i].name, strlen(items[i].name));
        if (items[i].units) memcpy(payload + 260, items[i].units, strlen(items[i].units));
        put32(payload + 516, items[i].type);
        uint32_t epsilon;
        memcpy(&epsilon, &items[i].epsilon, sizeof(epsilon));
        put32(payload + 520, epsilon);
        put32(payload + 524, (uint32_t)i);
        int r = packet(s, 0x0c, payload, sizeof(payload), NULL);
        if (r != SIMC_OK) return r;
    }
    return SIMC_OK;
}

int simc_clear_definition(simc_session_t *s, uint32_t id)
{
    uint8_t payload[4];
    put32(payload, id);
    return packet(s, 0x0d, payload, sizeof(payload), NULL);
}

int simc_request_data(simc_session_t *s, uint32_t request_id, uint32_t definition_id,
                      uint32_t period, uint32_t flags)
{
    if (period > SIMC_PERIOD_SECOND || (flags & ~SIMC_CHANGED)) return SIMC_INVALID_ARG;
    uint8_t payload[32] = {0};
    put32(payload, request_id);
    put32(payload + 4, definition_id);
    put32(payload + 12, period);
    put32(payload + 16, flags);
    return packet(s, 0x0e, payload, sizeof(payload), NULL);
}

int simc_set_double(simc_session_t *s, uint32_t id, double value, uint32_t *send_id)
{
    if (!isfinite(value)) return SIMC_INVALID_ARG;
    uint8_t payload[28] = {0};
    put32(payload, id);
    put32(payload + 12, 1);
    put32(payload + 16, 8);
    put_double(payload + 20, value);
    return packet(s, 0x10, payload, sizeof(payload), send_id);
}

static int named_event(simc_session_t *s, uint32_t packet_id, uint32_t id, const char *name)
{
    if (!valid_string(name)) return SIMC_INVALID_ARG;
    uint8_t payload[260] = {0};
    put32(payload, id);
    memcpy(payload + 4, name, strlen(name));
    return packet(s, packet_id, payload, sizeof(payload), NULL);
}

int simc_map_event(simc_session_t *s, uint32_t id, const char *name)
{
    return named_event(s, 4, id, name);
}

int simc_subscribe_system_event(simc_session_t *s, uint32_t id, const char *name)
{
    return named_event(s, 0x17, id, name);
}

int simc_send_event(simc_session_t *s, uint32_t id, const uint32_t data[5], uint32_t *send_id)
{
    if (!data) return SIMC_INVALID_ARG;
    uint8_t payload[36] = {0};
    put32(payload + 4, id);
    put32(payload + 8, 1);       // HIGHEST priority.
    put32(payload + 12, 0x10);   // GROUPID_IS_PRIORITY.
    for (unsigned i = 0; i < 5; ++i) put32(payload + 16 + 4 * i, data[i]);
    return packet(s, 0x44, payload, sizeof(payload), send_id);
}

int simc_decode_data(const simc_message_t *m, const simc_item_t *items,
                      size_t count, simc_value_t *out)
{
    if (!m || !out || !valid_items(items, count)) return SIMC_INVALID_ARG;
    // OBJECT_ID_USER (0) is a request selector, not a guaranteed response ID.
    // The caller correlates request/definition and, if needed, actual object ID.
    if (m->type != SIMC_MESSAGE_DATA ||
        (m->flags & ~SIMC_CHANGED) || m->count != count || !m->data) return SIMC_PROTOCOL_ERROR;
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i)
        bytes += items[i].type == SIMC_FLOAT64 ? 8 : 256;
    if (m->data_size != bytes) return SIMC_PROTOCOL_ERROR;
    const uint8_t *p = m->data;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].type == SIMC_FLOAT64) {
            out[i].number = get_double(p);
            p += 8;
        } else {
            memcpy(out[i].string, p, 256);
            out[i].string[255] = '\0';
            p += 256;
        }
    }
    return SIMC_OK;
}

const char *simc_error_name(int r)
{
    switch (r) {
    case SIMC_OK: return "OK (sent/received, not applied)";
    case SIMC_TIMEOUT: return "poll timeout";
    case SIMC_IO: return "connection/send/receive failure";
    case SIMC_PROTOCOL_ERROR: return "invalid or stalled frame";
    case SIMC_INVALID_ARG: return "invalid argument";
    case SIMC_NO_MEMORY: return "out of memory";
    case SIMC_QUIT: return "simulator quit";
    case SIMC_EXCEPTION: return "OPEN rejected by simulator";
    default: return "unknown error";
    }
}
