# simconnect-esp32

An ESP-IDF SimConnect TCP client with a Wi-Fi / BOOT-button test application for
**MSFS 2024 + FlyByWire A32NX release**.

**Status: library and example build successfully; host tests pass. Not yet flashed
by the agent. The user's first simulator test reached OPEN but exposed a response
validation issue; firmware 0.1.1 addresses the zero-object-ID assumption and adds
loading-state handling/diagnostics. Firmware 0.1.2 adds a probe/window heat RGB
indicator. Updated device validation is pending.**
No Node.js application, PC relay, SDK
binary, or WASM bridge is required for the numeric LVar example.

## Quick start

1. Follow [simulator network setup](docs/simulator-setup.md).
2. Copy the example configuration and fill in Wi-Fi credentials and the PC's IPv4:

   ```sh
   cp examples/fbw_external_power/main/app_config.h.example \
      examples/fbw_external_power/main/app_config.h
   ```

3. Activate ESP-IDF and build from this repository:

   ```sh
   source /path/to/esp-idf/export.sh
   bash build.sh
   ```

4. Follow the [example test instructions](examples/fbw_external_power/README.md)
   to flash and monitor when you are ready. Flashing replaces the installed
   application and partition table; back up anything you need first.

The application waits through simulator/aircraft loading before enabling BOOT.
BOOT GPIO0 toggles the cockpit EXT PWR selection after fresh simulator feedback.
It does **not** connect the ground-service GPU. Ground power must already be
available. The example rejects unknown aircraft, stale state, unavailable power
when selecting ON, and presses while an action is pending.

The onboard RGB LED (GPIO38 by default; GPIO48 on the tested board) mirrors the
PROBE/WINDOW HEAT Korry ON legend, including power, lamp test, and dimming.
Firmware 0.1.3 uses the v2024.1.0 cockpit source as its reference. This is a separate read-only
subscription; see the [read-to-LED walkthrough](docs/read-to-led.md).

Without `app_config.h`, the firmware builds but leaves Wi-Fi unconfigured and
reports the configuration requirement on the serial console, unless a complete
NVS configuration is present.

### Prebuilt ESP32-S3 firmware

Users with a **16 MB ESP32-S3-DevKitC-1** can flash a shared firmware binary and
generate a private Wi-Fi/simulator settings image without installing ESP-IDF.
See [downloads, configuration and flashing](docs/flashing.md).
Maintainers build GitHub Release assets with `python3 tools/package_release.py`;
generated binaries stay out of Git. Never distribute locally configured binaries.

## Library

- Persistent, single-owner TCP sessions; explicit dispatch and close.
- FLOAT64 SimVar/LVar definitions, requests, subscriptions, and writes.
- STRING256 reads for aircraft identity.
- Key-event mapping/transmission with five explicit data parameters.
- System-event subscription; exception and quit handling.
- Bounded frames and absolute connection/send/partial-frame deadlines.

This is a **subset**, not a complete port of `node-simconnect`. No H-event bridge,
Input Events, tagged payloads, arbitrary structures, DNS discovery, or automatic
command replay. The earlier prototype's convenience API is not source-compatible.

## Documentation

- [C API and integration](docs/api.md)
- [Simulator network setup](docs/simulator-setup.md)
- [Reading and writing SimVars](docs/simvars.md)
- [Events](docs/events.md)
- [LVars](docs/lvars.md)
- [Read a variable and drive an LED](docs/read-to-led.md)
- [H events and Input Events](docs/hvars-and-input-events.md)
- [Wi-Fi example](examples/fbw_external_power/README.md)
- [Compatibility and test evidence](docs/compatibility.md)
- [Provenance](docs/provenance.md) and [attribution](NOTICE.md)

## Checks

```sh
python3 tests/test_client.py
python3 tests/test_release_tools.py
python3 tools/check_docs.py
bash build.sh
```

Host tests require Python 3.9+ and a C compiler with UndefinedBehaviorSanitizer.
They compile the actual library and use synthetic local TCP peers; no simulator
or ESP32 is needed.

## License

GNU Lesser General Public License, version 3 or (at your option) any later version:
`LGPL-3.0-or-later`. Commercial use is permitted under its conditions.
See [LICENSE](LICENSE), [COPYING](COPYING), and [NOTICE.md](NOTICE.md).
Static firmware distributions must provide the required source/relinking material
and installation information where applicable.

Unofficial project; not affiliated with Microsoft, Espressif, or FlyByWire.
