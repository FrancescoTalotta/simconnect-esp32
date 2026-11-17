# Reading and writing SimVars

See the [implemented C API](api.md). The names below also identify the
corresponding SDK operations.

## Read one value

For example, read `PLANE ALTITUDE` in `feet` as FLOAT64:

1. Open a SimConnect session and wait for OPEN.
2. Allocate a data-definition ID and request ID within that session.
3. Use `AddToDataDefinition` with the name, units, and FLOAT64 datatype.
4. Use `RequestDataOnSimObject` for the user aircraft with period ONCE.
5. Dispatch packets until the matching request arrives or a deadline expires.
6. Validate request ID, definition ID, object ID, flags, count, and payload size.
7. Decode the value in definition order and little-endian wire format.

Standard SimVar names do not need the RPN `A:` prefix in a data definition.
Indexed variables use the documented suffix, such as `GENERAL ENG RPM:1`.

## Subscribe to changes

Use a recurring period such as SECOND or SIM_FRAME, and CHANGED when appropriate.
Set a meaningful epsilon in each data-definition item to filter at the simulator.
An additional local threshold can reduce application notifications but does not
save network bandwidth.

Without TAGGED, decode all returned fields in definition order. With TAGGED,
decode datum-ID/value pairs instead; never interpret them as a plain double array.
Do not silently accept truncated definitions.

Cancel the request with period NEVER before tearing down a subscription.
The current API uses explicit `simc_poll`, not callbacks. Borrowed payload data
lasts until the next poll/close; copy decoded values before handing off to another task.

## Write a value

Only variables marked settable in the SDK should be written. For a documented
settable numeric variable such as `KOHLSMAN SETTING HG:1`:

1. Confirm the current SDK's units and settable status.
2. Define it with the matching datatype and units (`inHg` for this example).
3. Send `SetDataOnSimObject` for the user aircraft with a correctly sized payload.
4. Keep dispatching exceptions, correlated with the outgoing send ID.
5. Read back the value if confirmation is needed.

A successful socket send is not proof that the aircraft applied a change.
Aircraft systems may overwrite a value or require an event/Input Event instead.
Do not assume every boolean or numeric SimVar can be written.

## Data types

FLOAT64 is appropriate for the initial numeric subset. It is not a universal
format for strings, structures, tagged data, or integer event hashes.
Never cast a received byte buffer to an arbitrary C struct without accounting
for alignment, packing, bounds, and endian representation.

## C example (inside the session owner)

```c
simc_item_t altitude = { "PLANE ALTITUDE", "feet", SIMC_FLOAT64, 0.5f };
int result = simc_add_definition(session, 10, &altitude, 1);
if (result == SIMC_OK)
    result = simc_request_data(session, 20, 10, SIMC_PERIOD_SECOND, SIMC_CHANGED);
// Check result, then integrate this branch into the owner's dispatch loop:
simc_message_t message;
result = simc_poll(session, 100, &message);
if (result == SIMC_OK && message.type == SIMC_MESSAGE_DATA &&
    message.request_id == 20 && message.definition_id == 10) {
    simc_value_t value;
    result = simc_decode_data(&message, &altitude, 1, &value);
    if (result == SIMC_OK) {
        // value.number is altitude in feet.
    }
}
// Also handle EXCEPTION, EVENT, TIMEOUT, and fatal results in the full dispatcher.
```

For a write, first create a separate definition containing exactly one writable
FLOAT64, then use `simc_set_double`. Reuse the definition for subsequent writes.
The full application shows reads, writes, confirmation, and error handling together.

References:
[AddToDataDefinition](https://docs.flightsimulator.com/html/Programming_Tools/SimConnect/API_Reference/Events_And_Data/SimConnect_AddToDataDefinition.htm),
[SimVars](https://docs.flightsimulator.com/html/Programming_Tools/SimVars/Simulation_Variables.htm).
