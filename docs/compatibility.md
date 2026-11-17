# Compatibility and validation

| Item | Intended target | Verification |
| --- | --- | --- |
| Simulator | Microsoft Flight Simulator 2024 | Exact build not recorded; not tested |
| Aircraft | FlyByWire A32NX release version for MSFS 2024 | Exact release number not recorded; not tested |
| MCU | ESP32-S3 | User confirmed |
| Module | ESP32-S3-WROOM-1 | User confirmed |
| Board reference | ESP32-S3-DevKitC-1 v1.1, N16R8 | User supplied official guide; schematic reviewed |
| Physical board | Amazon ASIN B0CB8MZKKB | Physical equivalence to reference board not tested |
| Flash / PSRAM | 16 MB / 8 MB | Confirmed for N16R8 in official ordering table; not tested on device |
| Network | On-board 2.4 GHz Wi-Fi | Not tested |
| BOOT switch | GPIO0, active-low, input with pull-up | SW1 shorts GPIO0 to GND in reference schematic |
| RGB LED | SK68XXMINI-HS, GPIO38 on v1.1, dim blue | Built-in RMT driver; physical output not tested |
| Console | USB-to-UART bridge, UART0 GPIO43 TX / GPIO44 RX | Reference board; no external peripherals assigned |
| ESP-IDF | Declared minimum 5.3; actual installed checkout below | Example build passed on the installed development checkout |
| Client | FLOAT64 / STRING256 subset, persistent TCP | Built for ESP32-S3 and tested against synthetic localhost peers |

The older ARINC project has `esp32s3`, 16 MB flash, W5500 networking, and two OTA
slots in its configuration. Its CMake source list excludes the old SimConnect
client. The new example has its own Wi-Fi initialization and no OTA.

## Board reference

Use the user-selected
[ESP32-S3-DevKitC-1 v1.1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)
and its
[November 2022 schematic](https://dl.espressif.com/dl/schematics/SCH_ESP32-S3-DevKitC-1_V1.1_20221130.pdf)
as the design baseline. The guide's ordering table specifies 16 MB flash and
8 MB PSRAM for ESP32-S3-DevKitC-1-N16R8.

SW1 connects GPIO0 to ground when pressed. Configure GPIO0 as an input with
pull-up; pressed reads zero. SW2 resets CHIP_PU/EN and must not be used as an
application input. The example needs no external wiring; firmware 0.1.2 adds a
small RMT-based driver for the onboard addressable RGB LED.

The guide identifies GPIO38 for the v1.1 addressable RGB LED; the initial board
revision uses GPIO48 instead. GPIO35–37 are unavailable for external use on
variants where the octal memory interface occupies them. Do not repurpose these
pins. Native USB uses GPIO19 D- and GPIO20 D+; prefer the USB-to-UART port for
the initial flashing/serial-monitor workflow.

These are reference-design facts, not physical validation of the purchased
board. Check for vendor deviations if the device behaves differently.
The test uses 16 MB flash, DIO / 40 MHz, and deliberately disables PSRAM;
no octal PSRAM settings are needed by the first test.

## Build and dependency evidence

Activate your installed SDK with `source /path/to/esp-idf/export.sh`.
The build evidence below records the development SDK used for validation,
without publishing personal installation paths.

- Header version: **6.0.0**.
- `idf.py --version` / Git describe:
  **v5.5-dev-3618-g7f6e7f4506-dirty**.
- SDK commit: `7f6e7f4506a7831f01e63591d17b468b3eda10c3`.
- Toolchain: Espressif Xtensa GCC 14.2.0, `esp-14.2.0_20241119`.
- Build target: `esp32s3`; example version 0.1.4.
- External ESP frameworks/plugins: **none**. All dependencies are SDK built-ins.
- RGB output uses the SDK's `esp_driver_rmt` channel/copy-encoder APIs, not an
  external led-strip package. No extra plugin version matrix is required.
- `tools/preflight.py` validates the SDK header range and writes the active
  version/commit to `examples/fbw_external_power/build/compatibility-evidence.txt`.

The differing header/describe versions and dirty SDK are recorded deliberately:
this is not claimed as a clean stable-release validation. Do not modify the
existing SDK to disguise that difference. A clean stable SDK build is a useful
follow-up.

Builds emitted SDK/tooling warnings (shell completion on macOS Bash 3.2,
esptool deprecated underscore options, mbedTLS CMake minimum-version deprecation,
Kconfig default notifications, and an unrelated certificate-bundle deprecation
during the initial all-components build). No project C compiler warnings were
emitted. The example now restricts component discovery to its actual dependency
graph and does not request the PSRAM component. These warnings are not evidence
of runtime validation.

The user tested the initial firmware: Wi-Fi and SimConnect OPEN succeeded, then
the state decoder rejected the first snapshot repeatedly. Later transport
failures were also reported. Those logs do not include response metadata, so the
exact rejected condition is not established.

Version 0.1.1 removes the invalid assumption that a user-aircraft response must
have object ID zero, adds rate-limited layout diagnostics, and waits through
flight/aircraft loading without closing a healthy TCP session. New gate tests
cover loading, stale data, changed aircraft/object IDs, and cancelled late
one-shot replies. The updated firmware has not yet been flashed or tested by the
agent against the device/simulator.

Version 0.1.2 adds a separate periodic read of `L:A32NX_MAN_PITOT_HEAT`, using a
single FLOAT64 definition. An independent output task controls the onboard RGB LED
and turns it off when data is stale or the flight/connection is not verified.
PROBE/WINDOW HEAT manual selection is not proof of actual heater operation.
The [walkthrough](read-to-led.md) records hardware timing sources and behavior.

Version 0.1.3 replaces selection-only indication with the Korry ON-legend
emissive logic traced to the published A32NX v2024.1.0 source: AC2 power,
manual selection, DC2-powered lamp test, and 10% DIM. The four inputs share one
definition/snapshot. The user's standalone test confirmed RGB operation on
GPIO48, configured in the ignored local header. The installed aircraft's exact
release and its agreement with this annunciator logic remain unverified.

Version 0.1.4 adds complete boot-time NVS configuration and an isolated public
release build that never includes the local credential header. The partition
layout is unchanged. GPIO38/48 and brightness are configurable in the private
NVS image. See [release packaging and flashing](flashing.md); device validation
of the new configuration path is still pending.

## BOOT input and power precautions

- Use documented GPIO0, active-low, with pull-up as the default.
- Account for its boot-strapping role: do not hold it while resetting/flashing.
- Ignore a button held at application startup until it is released.
- Use USB power for the initial example. Do not combine USB with external
  5 V/3.3 V header power; follow the guide's mutually exclusive supply options.
- Keep GPIO43/44 free for the reference board's USB-to-UART console.
- No OTA: `partitions.csv` allocates NVS at 0x9000 (24 KB), PHY at 0xf000 (4 KB),
  and a factory app at 0x10000 with size 0xff0000, ending at 16 MB.
  The large application partition is intentional growth headroom.

## Validation matrix

1. Host tests (implemented): fragmentation at every header boundary, coalesced frames, timeout
   after partial data, EOF, oversized/truncated frames, unknown message IDs,
   malformed data counts, and exception correlation.
   The actual C client is compiled with strict warnings and UndefinedBehaviorSanitizer.
   BOOT debounce, flight gating, LED policy, and GRB/timing helpers are also tested
   with AddressSanitizer/UndefinedBehaviorSanitizer.
2. ESP-IDF build: installed SDK above, `esp32s3` target. The local configuration is
   ignored by Git; locally configured binaries contain credentials and must not
   be shared. Public builds use a separate private NVS image instead.
3. Device (pending): Wi-Fi loss/recovery, simulator restart, aircraft change, idle heap
   stability, held button, bounce, and rapid repeated presses.
4. Aircraft (pending): unavailable GPU, available/off, available/on, manual cockpit changes,
   probe heat ON/AUTO LED behavior, and state synchronization after reconnect.

Record exact simulator, aircraft, ESP-IDF, board, and firmware revisions alongside
results. A passing build alone does not establish working simulator integration.
