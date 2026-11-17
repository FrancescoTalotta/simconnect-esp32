# C API

Public header: [simconnect_client.h](../include/simconnect_client.h).
The [test application](../examples/fbw_external_power/main/app_main.c) is the
complete Wi-Fi integration example.

## Integrating the component

Place this repository at `components/simconnect` in an ESP-IDF application
(or supply its path through `EXTRA_COMPONENT_DIRS`), then declare
`REQUIRES simconnect` in the consuming component.
The included example uses a CMake wrapper to keep that name stable regardless
of the checkout folder name.

Minimum declared ESP-IDF: 5.3. Only the installed development checkout listed in
[compatibility](compatibility.md) has been built; other releases are not yet
verified. There are no external framework dependencies.

## Ownership and lifetime

- Initialize Wi-Fi/Ethernet and wait for an IP before `simc_open`.
- One task owns a session and **all** calls, including `simc_close`.
- Queue commands from other tasks. There is no implicit locking/background task.
- `simc_poll` dispatches one message; poll continuously even when only writing.
- `message.data` is borrowed until the next poll or close. Decoded values are
  copied into caller-owned `simc_value_t` storage.
- Close and reopen after every fatal transport/protocol/quit result.
- Never replay an uncertain write automatically.

```c
simc_config_t config = {
    .host = "192.168.1.100",
    .port = "5111",
    .client_name = "ESP32 test",
    .protocol = SIMC_PROTOCOL_MSFS2020, // KittyHawk: works with MSFS 2024.
    .timeout_ms = 3000,
};
simc_session_t *session = NULL;
int result = simc_open(&session, &config);
// Check result before using session. Later, on the same task:
simc_close(session);
```

## Operations

| Function | Meaning |
| --- | --- |
| `simc_add_definition` | Append up to 16 FLOAT64/STRING256 fields to a definition |
| `simc_request_data` | Request ONCE, recurring periods, or cancel using NEVER |
| `simc_decode_data` | Validate and decode an untagged user-aircraft response |
| `simc_set_double` | Write exactly one FLOAT64 using its own definition |
| `simc_clear_definition` | Clear a definition after cancelling its active requests |
| `simc_map_event` | Map a key/client event name |
| `simc_send_event` | Transmit five DWORD parameters; returns outgoing send ID |
| `simc_subscribe_system_event` | Subscribe to a supported system notification |
| `simc_poll` | Receive DATA, EXCEPTION, EVENT, OTHER, or a fatal QUIT result |

Definition/request/event IDs are session-local and caller-managed. Use separate
IDs for unrelated requests; match both request and definition before decoding.
The decoder checks flags, count, and complete payload layout, but does not know
which request your application intended. `SIMCONNECT_OBJECT_ID_USER` (zero) is
a request selector, not a mandatory response object ID. Match request/definition
IDs and retain the returned object ID for identity checks across responses.

Period NEVER cancels the matching request. CHANGED supports server epsilon.
Do not combine CHANGED with STRING256 fields; the example requests complete
recurring snapshots without CHANGED. Tagged responses are unsupported.

`set_double` requires a definition containing exactly one FLOAT64. Sending does
not acknowledge application: dispatch exceptions and read back actual state.
The library does not keep a definition registry or enforce aircraft writability.

## Limits and errors

Hosts are numeric IPv4 addresses; ports are decimal 1..65535.
Names/client names are nonempty and at most 255 bytes; longer names are rejected,
not truncated. `units = NULL` encodes the null/empty units field for strings.
Connect/send/OPEN/partial-frame deadlines are configurable from 1 to 60000 ms.
Each received frame is bounded at 8192 bytes, with one reusable buffer per session.

TCP keepalive is enabled. Where supported, it uses 20 seconds idle, 5 seconds
between probes, and three missed probes. This detects a lost PC/route without
treating quiet simulator loading as a disconnected socket.

`SIMC_TIMEOUT` from **poll** is nonfatal and preserves partial data until its
absolute frame deadline. Other negative poll results are fatal. An OPEN failure,
including timeout, leaves `*out == NULL` and no open session.
`SIMC_MESSAGE_EXCEPTION` carries simulator error code, outgoing `send_id`, and
parameter index. In this case poll itself returns OK; the application must handle
the error. OPEN exceptions return `SIMC_EXCEPTION`.

No H-event bridge, Input Events, tagged data, arbitrary structures, or receive
event-ex decoding is implemented. Unknown bounded messages return OTHER.
