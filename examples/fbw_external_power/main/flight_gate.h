// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    FBW_WAIT_WIFI,
    FBW_WAIT_SIMULATOR,
    FBW_WAIT_FLIGHT,
    FBW_WAIT_AIRCRAFT,
    FBW_READY,
    FBW_CONFIRMING,
} fbw_state_t;

typedef struct {
    bool sim_known;
    bool sim_running;
    unsigned snapshots;
    uint32_t object_id;
    int64_t first_snapshot_ms;
    int64_t last_snapshot_ms;
    char title[256];
} flight_gate_t;

static inline bool flight_bool_valid(double value)
{
    return value == 0 || value == 1 || value == -1;
}

static inline const char *fbw_state_name(fbw_state_t state)
{
    switch (state) {
    case FBW_WAIT_WIFI: return "WAIT_WIFI";
    case FBW_WAIT_SIMULATOR: return "WAIT_SIMULATOR";
    case FBW_WAIT_FLIGHT: return "WAIT_FLIGHT";
    case FBW_WAIT_AIRCRAFT: return "WAIT_AIRCRAFT";
    case FBW_READY: return "READY";
    case FBW_CONFIRMING: return "CONFIRMING";
    default: return "UNKNOWN";
    }
}

// Invalidating aircraft readiness does not close the session or forget Sim state.
static inline void flight_gate_reset_aircraft(flight_gate_t *gate)
{
    gate->snapshots = 0;
    gate->title[0] = '\0';
    gate->last_snapshot_ms = 0;
}

static inline bool flight_gate_sim(flight_gate_t *gate, bool running)
{
    bool changed = !gate->sim_known || gate->sim_running != running;
    gate->sim_known = true;
    gate->sim_running = running;
    if (changed) flight_gate_reset_aircraft(gate);
    return changed;
}

static inline bool flight_gate_identity(const flight_gate_t *gate,
                                       const char *title, uint32_t object_id)
{
    return gate->snapshots && gate->object_id == object_id &&
           strcmp(gate->title, title) == 0;
}

static inline void flight_gate_observe(flight_gate_t *gate, const char *title,
                                      uint32_t object_id, int64_t now)
{
    if (!gate->sim_known || !gate->sim_running) return;
    if (!flight_gate_identity(gate, title, object_id) ||
        now - gate->last_snapshot_ms > 5000) {
        gate->snapshots = 1;
        gate->object_id = object_id;
        strncpy(gate->title, title, sizeof(gate->title) - 1);
        gate->title[255] = '\0';
        gate->first_snapshot_ms = now;
    } else if (now - gate->first_snapshot_ms >= 500 && gate->snapshots < 2) {
        // Two queued packets in a burst are not evidence of stable loading.
        gate->snapshots = 2;
    }
    gate->last_snapshot_ms = now;
}

static inline fbw_state_t flight_gate_state(flight_gate_t *gate, int64_t now)
{
    if (!gate->sim_known || !gate->sim_running) return FBW_WAIT_FLIGHT;
    if (gate->snapshots && now - gate->last_snapshot_ms > 5000)
        flight_gate_reset_aircraft(gate);
    return gate->snapshots >= 2 ? FBW_READY : FBW_WAIT_AIRCRAFT;
}

typedef struct {
    uint32_t next;
    uint32_t fresh;
    uint32_t confirm;
} flight_requests_t;

static inline void flight_requests_cancel(flight_requests_t *requests)
{
    requests->fresh = requests->confirm = 0;
}

// Never reuse an expired one-shot ID: a late reply must not trigger a new action.
static inline uint32_t flight_request_allocate(flight_requests_t *requests)
{
    if (requests->next < 2 || requests->next == UINT32_MAX) return 0;
    return requests->next++;
}
