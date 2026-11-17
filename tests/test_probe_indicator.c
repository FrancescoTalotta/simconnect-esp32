// SPDX-License-Identifier: LGPL-3.0-or-later
#include <assert.h>
#include <stdio.h>
#include "../examples/fbw_external_power/main/probe_indicator.h"
#include "../examples/fbw_external_power/main/led_frame.h"

int main(void)
{
    probe_sample_t sample = {
        .valid = true, .manual_on = true, .epoch = 3, .network_epoch = 2,
        .ac2_powered = true, .dc2_powered = true, .ann_mode = 1,
        .received_ms = 1000,
    };
    assert(probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 1100));
    assert(probe_indicator_should_light(&sample, FBW_CONFIRMING, true, 3, 2, 1100));
    // Loading, startup, and simulator waits must never display cached blue.
    for (fbw_state_t state = FBW_WAIT_WIFI; state <= FBW_WAIT_AIRCRAFT; ++state)
        assert(!probe_indicator_should_light(&sample, state, true, 3, 2, 1100));
    assert(!probe_indicator_should_light(&sample, FBW_READY, false, 3, 2, 1100));
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 4, 2, 1100));
    // A quick Wi-Fi reconnect must not revive a sample from the old connection.
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 3, 3, 1100));
    assert(probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 6000));
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 6001));
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 999));
    sample.valid = false;
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 1100));
    sample.valid = true;
    sample.manual_on = false;
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 1100));

    // Exhaustive cockpit truth table: power dominates selection and lamp test.
    for (unsigned manual = 0; manual < 2; ++manual)
        for (unsigned ac2 = 0; ac2 < 2; ++ac2)
            for (unsigned dc2 = 0; dc2 < 2; ++dc2)
                for (unsigned ann = 0; ann < 3; ++ann) {
                    sample.manual_on = manual;
                    sample.ac2_powered = ac2;
                    sample.dc2_powered = dc2;
                    sample.ann_mode = ann;
                    bool expected = ac2 && (manual || (ann == 0 && dc2));
                    assert(probe_indicator_should_light(
                        &sample, FBW_READY, true, 3, 2, 1100) == expected);
                    assert(probe_indicator_brightness(&sample, 16) == (ann == 2 ? 2 : 16));
                }
    sample.ann_mode = 3;
    assert(!probe_indicator_should_light(&sample, FBW_READY, true, 3, 2, 1100));
    sample.ann_mode = 2;
    assert(probe_indicator_brightness(&sample, 0) == 0);
    assert(probe_indicator_brightness(&sample, 1) == 1);
    assert(probe_indicator_brightness(&sample, 255) == 26);

    uint8_t grb[3];
    led_grb_bytes(0, 0, 16, grb);
    assert(grb[0] == 0 && grb[1] == 0 && grb[2] == 16);
    led_grb_bytes(0x12, 0x34, 0x56, grb);
    assert(grb[0] == 0x34 && grb[1] == 0x12 && grb[2] == 0x56);
    led_grb_bytes(0, 0, 0, grb);
    assert(grb[0] == 0 && grb[1] == 0 && grb[2] == 0);
    // Recover every possible byte from the MSB-first pulse durations.
    for (unsigned byte = 0; byte < 256; ++byte) {
        unsigned recovered = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            unsigned high = led_bit_high_ticks((uint8_t)byte, bit);
            assert(high == 3 || high == 9);
            assert(LED_BIT_TICKS - high == (high == 3 ? 9 : 3));
            recovered = (recovered << 1) | (high == LED_ONE_HIGH_TICKS);
        }
        assert(recovered == byte);
    }
    assert(LED_RESOLUTION_HZ == 10000000);
    assert(LED_FRAME_SYMBOLS == 25);
    assert(LED_RESET_HALF_TICKS * 2 == 3000); // 300 us reset.
    puts("Probe indicator: read-to-output gating, stale data, reconnects, GRB/timing passed");
    return 0;
}
