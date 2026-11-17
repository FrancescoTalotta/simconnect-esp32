#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Create a private 24 KiB NVS image without putting credentials in argv/history."""
import argparse
import csv
import getpass
import ipaddress
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def validate(ssid, password, host, port, aircraft, gpio, brightness):
    if not 1 <= len(ssid.encode("utf-8")) <= 32 or "\0" in ssid:
        raise ValueError("SSID must be 1..32 UTF-8 bytes without NUL")
    n = len(password.encode("utf-8"))
    if "\0" in password or not (
        n == 0 or 8 <= n <= 63 or
        (n == 64 and all(c in "0123456789abcdefABCDEF" for c in password))
    ):
        raise ValueError("Password must be empty (open Wi-Fi), 8..63 bytes, or 64 hex digits")
    ipaddress.IPv4Address(host)
    if not port.isascii() or not port.isdigit() or not 1 <= int(port) <= 65535:
        raise ValueError("Port must be 1..65535")
    if not 1 <= len(aircraft.encode("utf-8")) <= 255 or "\0" in aircraft:
        raise ValueError("Aircraft filter must be 1..255 UTF-8 bytes without NUL")
    if gpio not in (38, 48) or not 0 <= brightness <= 255:
        raise ValueError("GPIO must be 38 or 48; brightness must be 0..255")


def generate(output, ssid, password, host, port, aircraft, gpio, brightness):
    validate(ssid, password, host, port, aircraft, gpio, brightness)
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Output already exists; choose a new filename")
    with tempfile.TemporaryDirectory(prefix="simconnect-settings-") as directory:
        csv_path = Path(directory) / "settings.csv"
        image = Path(directory) / "settings.bin"
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["key", "type", "encoding", "value"])
            writer.writerow(["simconnect", "namespace", "", ""])
            for key, value in (
                ("ssid", ssid), ("password", password), ("host", host),
                ("port", port), ("aircraft", aircraft),
            ):
                writer.writerow([key, "data", "string", value])
            writer.writerow(["rgb_gpio", "data", "u8", gpio])
            writer.writerow(["brightness", "data", "u8", brightness])
        subprocess.run(
            [sys.executable, "-m", "esp_idf_nvs_partition_gen", "generate",
             str(csv_path), str(image), "0x6000"], check=True,
        )
        data = image.read_bytes()
        if len(data) != 0x6000:
            raise ValueError("Unexpected NVS image size")
        # Exclusive creation avoids overwriting files or following existing symlinks.
        fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("user-settings.bin"))
    parser.add_argument("--port", default="5111", help="SimConnect TCP port")
    parser.add_argument("--aircraft", default="FlyByWire")
    parser.add_argument("--rgb-gpio", type=int, choices=(38, 48), default=38)
    parser.add_argument("--brightness", type=int, default=16)
    args = parser.parse_args()
    try:
        ssid = input("2.4 GHz Wi-Fi SSID: ")
        password = getpass.getpass("Wi-Fi password (empty for open Wi-Fi): ")
        host = input("Simulator PC IPv4 address: ").strip()
        generate(args.output, ssid, password, host, args.port,
                 args.aircraft, args.rgb_gpio, args.brightness)
    except (ValueError, OSError, subprocess.CalledProcessError, EOFError) as error:
        sys.exit(f"Could not generate settings: {error}")
    print(f"Private image: {args.output}. Contains plaintext credentials; NEVER upload it.")
    print("Flashing it at 0x9000 replaces ALL existing NVS data. See FLASH.md/docs/flashing.md.")


if __name__ == "__main__":
    main()
