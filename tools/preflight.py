#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Check the active SDK and write nonsecret, generated build evidence."""
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    idf = Path(os.environ.get("IDF_PATH", ""))
    header = idf / "components/esp_common/include/esp_idf_version.h"
    if not header.is_file():
        sys.exit("ESP-IDF is not active: source its export.sh")
    text = header.read_text()
    version = tuple(int(re.search(rf"#define ESP_IDF_VERSION_{part}\s+(\d+)", text)[1])
                    for part in ("MAJOR", "MINOR", "PATCH"))
    if version < (5, 3, 0):
        sys.exit("ESP-IDF 5.3 or newer is required")
    reported = subprocess.check_output(["idf.py", "--version"], text=True).strip()
    revision = subprocess.check_output(["git", "-C", str(idf), "rev-parse", "HEAD"],
                                       text=True).strip()
    report = (
        f"SDK headers: {'.'.join(map(str, version))}\n"
        f"SDK reported: {reported}\nSDK commit: {revision}\n"
        "Target: esp32s3; flash: 16 MB DIO 40 MHz; PSRAM: disabled; OTA: none\n"
        "External ESP frameworks/plugins: none (ESP-IDF built-ins only)\n"
        "RGB output: built-in esp_driver_rmt; single SK68XXMINI-HS on GPIO38 by default\n"
        "Declared SDK range: >=5.3; only the actual SDK/build result proves this baseline\n"
        "No credentials included in this report\n"
    )
    output = Path(__file__).resolve().parents[1] / "examples/fbw_external_power/build"
    output.mkdir(exist_ok=True)
    (output / "compatibility-evidence.txt").write_text(report)
    print(report, end="")


if __name__ == "__main__":
    main()
