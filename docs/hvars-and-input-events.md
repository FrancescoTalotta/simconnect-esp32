# H events and Input Events

## “HVars” are events, not stored values

`H:` identifies an HTML event delivered to an aircraft's JavaScript instrument.
There is no generic current HVar value to read.

A common integration uses a simulator-side WASM module to invoke the Gauge API's
`execute_calculator_code` with an expression such as:

```text
(>H:AN_AIRCRAFT_SPECIFIC_EVENT)
```

This is illustrative RPN, not a real universal event name. The aircraft must
handle the named event. Do not pass `H:...` to `AddToDataDefinition` or assume
mapping it as a key event will execute RPN.

The ESP32 cannot execute the simulator's Gauge API locally. A bridge needs an
explicit protocol (for example SimConnect client-data areas), an installed
simulator-side component, error responses, and documented version compatibility.
No such bridge is included or currently planned for the first example.

## Input Events / BVars

These are a different API, not an alias for H events. Modern aircraft may expose
controls using SimConnect's Input Event operations:

1. Enumerate the loaded aircraft's available Input Events.
2. Resolve names to their 64-bit hashes.
3. Inspect parameter types.
4. Get, set, or subscribe using the appropriate SDK operation.
5. Invalidate cached aircraft-specific mappings after an aircraft change.

Preserve hashes as `uint64_t`; never round them through a double.
Input Event support and exposed controls vary by aircraft. Do not assume every
RPN `B:` suffix is enumerated or that every operation is supported.

Neither H-event bridging nor Input Events are implemented in this repository.
The EXT PWR example uses documented numeric LVars directly.

References:
[RPN variable types](https://docs.flightsimulator.com/html/Additional_Information/Reverse_Polish_Notation.htm),
[Gauge calculator API](https://docs.flightsimulator.com/html/Programming_Tools/WASM/Gauge_API/execute_calculator_code.htm),
[MSFS 2024 API reference](https://docs.flightsimulator.com/msfs2024/html/6_Programming_APIs/SimConnect/SimConnect_API_Reference.htm).
