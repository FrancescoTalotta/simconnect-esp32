# Flash the prebuilt ESP32-S3 example

## Supported hardware

ESP32-S3-DevKitC-1 with **16 MB flash**, onboard BOOT GPIO0, and an onboard
addressable RGB LED on GPIO38 (v1.1) or GPIO48 (original v1.0).
PSRAM is unused. No OTA. These binaries are **not universal ESP32 binaries**:
do not use them on ESP32, C3, S2, or smaller-flash boards.
Use the USB-to-UART port; the console is 115200 baud.

Build-tested only; validate on your own simulator bench before relying on it.
Use a trusted LAN, not real aircraft or safety-critical equipment.

## Downloads and tools

Download and extract `esp32s3-firmware.zip` from the same GitHub Release as
`source.zip` and `SHA256SUMS`. Verify downloaded ZIP hashes
against `SHA256SUMS` (for example `shasum -a 256 *.zip`).
The shared firmware contains **no configured Wi-Fi credentials**.
If no release is published yet, a maintainer must generate and attach these assets.

Install Python 3.10+ and the flashing/settings tools (ESP-IDF is not needed):

```sh
python -m pip install esptool esp-idf-nvs-partition-gen
```

Run all following commands from the extracted firmware directory.

## Create your private configuration

```sh
python make_settings.py --rgb-gpio 38
```

Use `--rgb-gpio 48` for the original v1.0 board. Choose the pin based on your
board revision; GPIO48 is used on the developer's tested board.
The script prompts for SSID, password (hidden), and simulator PC IPv4 address.
Optional flags: `--port 5111`, `--aircraft FlyByWire`, `--brightness 16`,
`--output user-settings.bin`. Passwords must be empty for open Wi-Fi, 8..63
UTF-8 bytes, or a 64-digit hexadecimal WPA key.

**Never upload/share `user-settings.bin`: it contains plaintext credentials.**
NVS and flash are not encrypted in this bench-test firmware. Anyone with flash
access can recover credentials. Temporary plaintext CSV is removed when the
script exits normally; the generated private image is kept for you.
Keep it outside public folders and delete it when no longer needed.

Configure the simulator's SimConnect TCP endpoint to match the supplied address
and port, using the repository's `docs/simulator-setup.md` instructions.
Aircraft matching is case-sensitive and defaults to `FlyByWire`.

## Flash (only when ready)

**Back up existing firmware/data first.** Flashing replaces firmware and the
partition table. Writing `user-settings.bin` replaces the **entire NVS partition**,
including any previous application's settings. No erase-all is necessary.
Do not use these instructions on devices with Secure Boot or flash encryption
enabled. Do not burn eFuses.

Replace `PORT` with your USB-to-UART device (for example `COM5` on Windows,
`/dev/cu.usbserial-…` on macOS, `/dev/ttyUSB0` on Linux):

```sh
python -m esptool --chip esp32s3 --port PORT write-flash --flash-mode dio --flash-freq 40m --flash-size 16MB 0x0 bootloader.bin 0x8000 partition-table.bin 0x9000 user-settings.bin 0x10000 fbw_external_power.bin
```

If automatic download mode fails, hold BOOT, tap RESET, release BOOT, and retry.
After flashing, release BOOT and reset to run normally.

| Offset | File |
| --- | --- |
| `0x0` | `bootloader.bin` |
| `0x8000` | `partition-table.bin` |
| `0x9000` | **Private** `user-settings.bin` (24 KiB) |
| `0x10000` | `fbw_external_power.bin` |

To change settings later, regenerate a private image using a new filename and
write **only** that image at `0x9000`, then reset. It still replaces all NVS data.
Firmware-only updates can omit `0x9000` to retain settings when the partition
layout is unchanged.

Open a serial terminal at 115200 baud. Commands: `help`, `status`, `settings`,
`heap`, `tasks`, `log`. Missing/invalid settings leave networking and outputs
disabled, with diagnostics on the console. Settings are read once at boot.

Load the A32NX at a stand and connect ground power using ground services.
Wait for `READY` before pressing BOOT to toggle EXT PWR. The LED mirrors the
PROBE/WINDOW HEAT ON legend; this does not indicate actual heating.
See `examples/fbw_external_power/README.md` in the source archive for full tests.

## Maintainers: create release assets

From the repository root, activate ESP-IDF and run:

```sh
source /path/to/esp-idf/export.sh
python tools/package_release.py
```

The tool builds a fresh source copy with `CONFIG_APP_PUBLIC_FIRMWARE=y`.
Local `app_config.h`, `sdkconfig`, caches and existing binaries are excluded;
the public flag also prevents inclusion of a local header. Your development
build and credentials are not modified. It never flashes or uploads.

Upload **all** assets from `dist/esp32s3-VERSION/` to the matching GitHub Release:

- `esp32s3-firmware.zip`: only the three flash binaries, instructions, settings
  generator, SDK build evidence, and license notices.
- `source.zip`: corresponding library and application sources, build/configuration
  instructions, SDK patch, and generated `sdkconfig.release`, allowing recipients
  to modify, rebuild and relink the application.
- `SHA256SUMS`: ZIP checksums.

Keep `dist/` and all `build/` directories out of Git. Keep tests and development
tools in Git; users do not need them to flash a release.

### Source and relinking

Firmware is LGPL-3.0-or-later; distribute the corresponding library/application
source and rebuild/relink instructions alongside binaries, with the notices.
Raw build directories are not release assets: debug files, caches and generated
commands can expose personal installation paths. The ESP-IDF components carry
their own licenses. SDK version/commit is recorded in `compatibility-evidence.txt`.
If using a modified SDK, also make those modifications available with the release.
The packager includes tracked SDK changes in `source.zip` as
`esp-idf-local.patch` and refuses SDKs with untracked files.

For a portable rebuild, extract `source.zip`, activate that ESP-IDF revision, copy
`examples/fbw_external_power/sdkconfig.release` to the same directory as `sdkconfig`,
and run `bash build.sh`. The release defaults already select public/NVS settings.
Apply `esp-idf-local.patch` in the SDK checkout first if it is nonempty.
You may modify the LGPL library, rebuild, and flash the resulting binaries using
the same offsets. No signing key or installation lock is required.
Rebuilding the supplied source generates fresh ELF, object files, static
libraries and linker commands in your own build directory for inspection and
relinking, without relying on paths from the maintainer's machine.
