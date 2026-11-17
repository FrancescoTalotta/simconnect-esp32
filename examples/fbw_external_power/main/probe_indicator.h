// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "flight_gate.h"

typedef struct {
    bool valid;
    bool manual_on;
    bool ac2_powered;
    bool dc2_powered;
    unsigned ann_mode; // 0=TEST, 1=BRT, 2=DIM (v2024.1.0 cockpit template).
    uint32_t epoch;
    uint32_t network_epoch;
    int64_t received_ms;
} probe_sample_t;

static inline bool probe_indicator_should_light(const probe_sample_t *sample,
                                                fbw_state_t state, bool connected,
                                                uint32_t epoch, uint32_t network_epoch,
                                                int64_t now)
{
    return sample->valid && sample->ann_mode <= 2 && sample->ac2_powered &&
           (sample->manual_on || (sample->ann_mode == 0 && sample->dc2_powered)) &&
           connected &&
           (state == FBW_READY || state == FBW_CONFIRMING) &&
           sample->epoch == epoch && sample->network_epoch == network_epoch &&
           now >= sample->received_ms && now - sample->received_ms <= 5000;
}

static inline uint8_t probe_indicator_brightness(const probe_sample_t *sample,
                                                 uint8_t full_brightness)
{
    // Round 10% to the nearest PWM step, retaining visible nonzero output.
    if (!full_brightness || sample->ann_mode != 2) return full_brightness;
    unsigned dim = (full_brightness + 5u) / 10u;
    return (uint8_t)(dim ? dim : 1);
}
