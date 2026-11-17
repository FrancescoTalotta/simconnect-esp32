# Wi-Fi external power and probe-heat LED test

**Build-tested, not yet device/simulator-tested.** Use only for a simulator bench
test, not real aircraft or safety-critical equipment.

## Hardware and software

- ESP32-S3-DevKitC-1 v1.1 reference, ESP32-S3-WROOM-1 N16R8.
- 16 MB flash, DIO at 40 MHz; PSRAM deliberately disabled (not needed).
- BOOT GPIO0, active-low input with pull-up; no external wiring.
- Addressable onboard RGB LED GPIO38, driven by built-in ESP-IDF RMT.
- USB-to-UART port for power, flashing, and 115200-baud console.
- MSFS 2024 and FlyByWire A32NX release, on a reachable trusted LAN.

Sources: [Espressif guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html),
[schematic](https://dl.espressif.com/dl/schematics/SCH_ESP32-S3-DevKitC-1_V1.1_20221130.pdf).
Do not hold BOOT during reset; that selects download mode.
Do not combine USB power with external header power.

## Configure

**Using prebuilt firmware?** Follow [release flashing and NVS configuration](../../docs/flashing.md)
instead; no source build or local header is required. Complete NVS settings take
precedence over the source-build header below. Invalid/incomplete NVS settings
disable networking and controls rather than mixing configurations.

From the repository root:

For a new checkout only (do not overwrite an existing local configuration):

```sh
cp examples/fbw_external_power/main/app_config.h.example \
   examples/fbw_external_power/main/app_config.h
```

Edit `main/app_config.h` in this example:

| Define | Meaning |
| --- | --- |
| `APP_WIFI_SSID` | 2.4 GHz Wi-Fi SSID |
| `APP_WIFI_PASSWORD` | Wi-Fi password |
| `APP_SIM_HOST` | Numeric LAN IPv4 of the simulator PC, not localhost |
| `APP_SIM_PORT` | TCP port configured in SimConnect.xml, default 5111 |
| `APP_AIRCRAFT_MATCH` | Case-sensitive substring of the aircraft TITLE, default `FlyByWire` |
| `APP_RGB_LED_GPIO` | Default 38 for v1.1; 48 only for the original v1.0 board |
| `APP_RGB_LED_BRIGHTNESS` | Blue-channel level 0..255, default 16; 0 disables LED output |

The RGB settings have defaults and need not be added to an existing local header.

The local header is ignored by Git. Credentials are compiled into the binary
but are not printed or persisted by the Wi-Fi driver (RAM storage). Do not share
your configured binary, header, or build directory.

If the default title filter does not match your release, the firmware logs the
actual TITLE and rejects control. Verify the aircraft before changing the
substring. Empty matches are rejected. This is a bench-test identity check,
not a cryptographic identity guarantee.

Follow [network setup](../../docs/simulator-setup.md). Load the A32NX at a stand
and connect ground power using ground services; verify AVAIL in the cockpit.

## Build

From the repository root:

```sh
source /path/to/esp-idf/export.sh
bash build.sh
```

Artifacts: `build/fbw_external_power.bin`, `build/bootloader/bootloader.bin`,
`build/partition_table/partition-table.bin`, and `build/fbw_external_power.elf`
under this example directory. The ELF supports debugging and relinking.

## Flash and monitor (run only when ready)

Flashing changes the installed app and partition table. Back up any existing
firmware/data you need. This example has **no OTA** and uses a single factory
partition with the rest of the 16 MB allocated as application growth room.

From this example directory, after activating ESP-IDF:

```sh
idf.py -p /dev/cu.YOUR_USB_UART_PORT flash monitor
```

Replace the port with the actual USB-to-UART device. No flashing is performed by
`build.sh`. Avoid `erase-flash` unless you explicitly intend to lose device data.
NVS initialization failures are reported rather than silently erasing NVS.

## Test procedure

1. Expect Wi-Fi IP, SimConnect OPEN, and loading states, then aircraft TITLE and
   EXT PWR state logs. Allow aircraft systems to finish loading. BOOT is enabled
   only when the logged state reaches READY.
2. Release BOOT if it was held at startup.
3. Press and release BOOT. Expect “ON sent … awaiting observed state”, followed by
   “ON confirmed by simulator”. Verify the cockpit button.
4. Press again; expect OFF confirmation.
5. Change the cockpit button with the mouse; the next press must use that actual
   state, not an independent ESP32 toggle.
6. Hold BOOT: only one action should occur. Rapid presses during confirmation
   are intentionally ignored.
7. Disconnect ground power: an ON request should be refused.
8. Restart the simulator or interrupt Wi-Fi: readiness must disappear; no old
    press should execute when the connection returns.
9. Select PROBE/WINDOW HEAT ON with AC BUS 2 powered: the onboard RGB LED should
   turn blue. AUTO normally turns it off; powered ANN LT TEST lights it even in
   AUTO. DIM reduces brightness to 10%. Loading, invalid/stale data, or a
   disconnected session must also turn it off.

See the [read-variable → LED walkthrough](../../docs/read-to-led.md) for the
definition, subscription, decoding, and physical-output code. This mirrors manual
Korry ON legend using v2024.1.0 cockpit logic, **not actual heating**
(which may be automatic in AUTO). GPIO48 was confirmed on the user's board.

## Loading states (firmware 0.1.1 and newer)

```text
WAIT_WIFI -> WAIT_SIMULATOR -> WAIT_FLIGHT -> WAIT_AIRCRAFT -> READY
                                                               |
                                                          CONFIRMING
```

- WAIT_WIFI: no network address yet.
- WAIT_SIMULATOR: retrying TCP/OPEN with 1, 2, 4, then 8-second backoff.
- WAIT_FLIGHT: connected, waiting for the `Sim` running notification.
- WAIT_AIRCRAFT: running flight, but aircraft identity/state is not yet usable.
- READY: two matching full snapshots, at least 500 ms apart; BOOT enabled.
- CONFIRMING: refreshing state or waiting for the requested switch state.

`Sim=0`, `AircraftLoaded`, changed TITLE/object ID, or stale/invalid data clears
readiness and discards pending actions. Loading does **not** close the SimConnect
connection. A 5-second snapshot gap returns to a waiting state; a 3-second action
deadline discards the action and requires fresh verification, without replay.
Each one-shot request has a new ID so a late reply cannot trigger a later action.
Actual socket, frame, quit, or simulator-exception failures still reconnect.

The firmware subscribes to a full snapshot every second, requests a fresh
snapshot before each action, and polls for confirmation after a write.
These checks establish usable state, not proof that every aircraft subsystem has
completed startup.

If data is still rejected after loading, a rate-limited `State layout mismatch`
line prints request/definition/object IDs, flags, count, and byte length.
Send that line for diagnosis. Bounds/count/flags validation remains strict;
the firmware does not guess at unknown layouts or enable controls anyway.

## Serial terminal

Use `help`, `status`, `settings`, `heap`, `tasks`, or `log <tag|*> <0..5>`.
Commands support the IDF console's history/completion. Settings output hides
credentials; change the local header and rebuild for source builds, or generate
and flash a new private NVS settings image for prebuilt firmware, then reset.

## Aircraft interface

| Name | Type | Access |
| --- | --- | --- |
| `TITLE` | STRING256 | Read identity |
| `L:A32NX_OVHD_ELEC_EXT_PWR_PB_IS_ON` | FLOAT64, number | Read/write selection |
| `L:A32NX_EXT_PWR_AVAIL:1` | FLOAT64, number | Read availability |
| `L:A32NX_MAN_PITOT_HEAT` | FLOAT64, number | Read manual PROBE/WINDOW HEAT selection; drives blue LED |
| `L:A32NX_ELEC_AC_2_BUS_IS_POWERED` | FLOAT64, number | Power gate for the heat Korry ON legend |
| `L:A32NX_ELEC_DC_2_BUS_IS_POWERED` | FLOAT64, number | Enables the ANN LT TEST override |
| `L:A32NX_OVHD_INTLT_ANN` | FLOAT64, number | 0=TEST, 1=BRT, 2=DIM |

Source: [FlyByWire Flight Deck API](https://docs.flybywiresim.com/aircraft/a32nx/a32nx-api/a32nx-flightdeck-api).
Check these names against the installed release. Selection does not prove every
bus is powered: generator priorities still apply.

## Remaining device checks

- [ ] BOOT polarity/debounce and startup-held behavior.
- [ ] Actual TITLE and LVars on the installed MSFS 2024 A32NX release.
- [ ] Available/off/on and manual cockpit state changes.
- [ ] Wi-Fi loss, simulator restart, aircraft change, and no action replay.
- [ ] Long-run heap/stack margins and console behavior.
- [ ] LED color, manual ON/AUTO indication, and off during stale/disconnected states.

Record exact simulator and aircraft versions with results.
