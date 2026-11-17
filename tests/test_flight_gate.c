// SPDX-License-Identifier: LGPL-3.0-or-later
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../examples/fbw_external_power/main/flight_gate.h"

int main(void)
{
    flight_gate_t gate = {0};
    assert(flight_gate_state(&gate, 0) == FBW_WAIT_FLIGHT);
    // Data alone must not enable controls before a running-flight notification.
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 100);
    assert(flight_gate_state(&gate, 100) == FBW_WAIT_FLIGHT);
    assert(flight_gate_sim(&gate, false));
    assert(flight_gate_state(&gate, 200) == FBW_WAIT_FLIGHT);
    assert(flight_gate_sim(&gate, true));
    assert(flight_gate_state(&gate, 300) == FBW_WAIT_AIRCRAFT);

    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 1000);
    assert(flight_gate_state(&gate, 1000) == FBW_WAIT_AIRCRAFT);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 1010);
    assert(flight_gate_state(&gate, 1010) == FBW_WAIT_AIRCRAFT);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 2000);
    assert(flight_gate_state(&gate, 2000) == FBW_READY);
    assert(!flight_gate_sim(&gate, true)); // Duplicate Sim=1 must not reset readiness.
    assert(flight_gate_state(&gate, 2100) == FBW_READY);

    // No snapshots during loading: wait, without requiring a new TCP session.
    assert(flight_gate_state(&gate, 7001) == FBW_WAIT_AIRCRAFT);
    assert(gate.sim_known && gate.sim_running);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 8000);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 9000);
    assert(flight_gate_state(&gate, 9000) == FBW_READY);

    // AircraftLoaded/invalid layout resets aircraft only.
    flight_gate_reset_aircraft(&gate);
    assert(flight_gate_state(&gate, 9100) == FBW_WAIT_AIRCRAFT);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 10000);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 11000);
    assert(flight_gate_state(&gate, 11000) == FBW_READY);
    // Actual object IDs may be nonzero and change across flights.
    flight_gate_observe(&gate, "FlyByWire A32NX", 99, 12000);
    assert(flight_gate_state(&gate, 12000) == FBW_WAIT_AIRCRAFT);
    assert(!flight_gate_identity(&gate, "FlyByWire A32NX", 42));
    flight_gate_observe(&gate, "FlyByWire A32NX", 99, 13000);
    assert(flight_gate_state(&gate, 13000) == FBW_READY);
    flight_gate_observe(&gate, "FlyByWire A32NX different livery", 99, 14000);
    assert(flight_gate_state(&gate, 14000) == FBW_WAIT_AIRCRAFT);
    assert(flight_gate_sim(&gate, false));
    assert(flight_gate_state(&gate, 15000) == FBW_WAIT_FLIGHT);
    flight_gate_observe(&gate, "FlyByWire A32NX", 42, 16000);
    assert(flight_gate_state(&gate, 16000) == FBW_WAIT_FLIGHT);

    // Late one-shot replies cannot belong to the next action after cancellation.
    flight_requests_t requests = { .next = 3 }; // 1=state and 2=probe heat.
    requests.fresh = flight_request_allocate(&requests);
    assert(requests.fresh > 2);
    uint32_t old_fresh = requests.fresh;
    requests.confirm = flight_request_allocate(&requests);
    uint32_t old_confirm = requests.confirm;
    flight_requests_cancel(&requests);
    assert(!requests.fresh && !requests.confirm);
    requests.fresh = flight_request_allocate(&requests);
    requests.confirm = flight_request_allocate(&requests);
    assert(requests.fresh != old_fresh && requests.fresh != old_confirm);
    assert(requests.confirm != old_fresh && requests.confirm != old_confirm);
    requests.next = UINT32_MAX;
    assert(flight_request_allocate(&requests) == 0);

    assert(flight_bool_valid(0) && flight_bool_valid(1) && flight_bool_valid(-1));
    assert(!flight_bool_valid(42) && !flight_bool_valid(NAN) && !flight_bool_valid(INFINITY));
    puts("Flight gate: loading, stale data, aircraft changes, late replies, booleans passed");
    return 0;
}
