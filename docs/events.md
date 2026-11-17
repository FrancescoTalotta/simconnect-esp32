# Events

Key-event transmission and basic system-event reception are implemented in the
[C API](api.md). Notification-group management and event-ex reception are not.

## Send a key event

1. Find the event in the simulator or aircraft documentation.
2. Map a session-local client event ID with `MapClientEventToSimEvent`.
3. Transmit the mapped event with its documented parameter(s).
4. Dispatch exceptions and observe the resulting SimVar/LVar state.

For example, `HEADING_BUG_INC` requests an increment, not a stored value.
Do not prepend the RPN `K:` prefix when mapping a conventional SimConnect event
name. Aircraft-specific custom event names must match their documentation.

Use explicit set operations instead of toggles when resynchronization matters.
Repeated toggles are not idempotent: never automatically replay a button action
after a reconnect.

## Parameters

The traditional transmit operation carries one DWORD. The extended operation
carries up to five DWORD parameters. Parameter order, scaling, signed encoding,
and meaning depend on the event. The second parameter is **not always an index**.
Expose `data0` through `data4` rather than assigning universal meanings.

Choose notification-group/priority flags deliberately. Do not confuse a group
ID with a priority value.

## Receive an event

Events do not have a general “get current value” operation.

- Use `SubscribeToSystemEvent` for supported system notifications, such as Pause.
- For supported key/client event notifications, the SDK requires the relevant
  notification group and priority. Group-management APIs are not yet ported.
- Read a corresponding SimVar/LVar for persistent aircraft state.
- Dispatch QUIT, disconnect, and exceptions as lifecycle events.

Not every cockpit action is exposed as a subscribable key event. H events and
Input Events have different mechanisms; see [their guide](hvars-and-input-events.md).

## C example (inside the session owner)

```c
int result = simc_map_event(session, 30, "HEADING_BUG_INC");
if (result == SIMC_OK) {
    const uint32_t data[5] = { 0, 0, 0, 0, 0 };
    uint32_t send_id;
    result = simc_send_event(session, 30, data, &send_id);
}
// Check result and continue polling for exceptions/state updates.
```

## Session requirements

Use one owner for session I/O and outgoing send IDs. Callers should enqueue
commands rather than write fragments concurrently into a shared TCP stream.
Report “queued/sent” separately from state confirmation. Keep reads active for
write-only applications so exceptions and quit messages cannot accumulate.

Reference:
[SDK events and data](https://docs.flightsimulator.com/msfs2024/html/6_Programming_APIs/SimConnect/SimConnect_API_Reference.htm).
