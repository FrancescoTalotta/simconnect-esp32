#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Build credential-free ESP32-S3 release assets in an isolated source copy."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = (
    ".gitignore", "CMakeLists.txt", "idf_component.yml", "LICENSE", "COPYING",
    "NOTICE.md", "README.md", "CONTRIBUTING.md", "build.sh",
)
SOURCE_DIRS = ("src", "include", "docs", "examples", "tools", "tests")
EXCLUDED_DIRS = {"build", ".cache", "__pycache__", "managed_components"}
EXCLUDED_FILES = {"app_config.h", "sdkconfig", "sdkconfig.old", "dependencies.lock"}


def copy_source(destination):
    for name in ROOT_FILES:
        shutil.copy2(ROOT / name, destination / name)
    for name in SOURCE_DIRS:
        for path in (ROOT / name).rglob("*"):
            relative = path.relative_to(ROOT)
            if (any(p in EXCLUDED_DIRS or p.startswith(".") for p in relative.parts)
                    or path.name in EXCLUDED_FILES or path.is_symlink()
                    or path.name.endswith(("-settings.bin", "-settings.csv", ".pyc"))):
                continue
            if path.is_file():
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)


def archive(source, output):
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                zipped.write(path, path.relative_to(source))


def archive_source(source, output):
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(source.rglob("*")):
            relative = path.relative_to(source)
            if (path.is_file() and path.name not in EXCLUDED_FILES
                    and not any(p in EXCLUDED_DIRS for p in relative.parts)):
                zipped.write(path, relative)


def package(output):
    # Refuse overwrite; never publish or use an existing, locally configured build.
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    work_parent = ROOT / "build"
    work_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-", dir=work_parent) as work:
        assets = Path(work) / "assets"
        assets.mkdir()
        source = Path(work) / "source"
        source.mkdir()
        copy_source(source)
        example = source / "examples/fbw_external_power"
        defaults = example / "sdkconfig.defaults"
        defaults.write_text(defaults.read_text() + "\nCONFIG_APP_PUBLIC_FIRMWARE=y\n")
        # Uses the same preflight, docs checks, and build wrapper as development.
        subprocess.run(["bash", "build.sh"], cwd=source, check=True)
        build = example / "build"
        config = (example / "sdkconfig").read_text()
        if "CONFIG_APP_PUBLIC_FIRMWARE=y\n" not in config:
            raise RuntimeError("Public firmware flag missing; refusing to package")
        flash = json.loads((build / "flasher_args.json").read_text())
        expected = {
            "0x0": "bootloader/bootloader.bin",
            "0x8000": "partition_table/partition-table.bin",
            "0x10000": "fbw_external_power.bin",
        }
        if flash["flash_files"] != expected:
            raise RuntimeError("Unexpected flash layout; update the release instructions first")
        files = flash["flash_settings"]
        if files != {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "40m"}:
            raise RuntimeError(f"Unexpected flash settings: {files}")
        public = Path(work) / "firmware"
        public.mkdir()
        for relative in expected.values():
            shutil.copy2(build / relative, public / Path(relative).name)
        shutil.copy2(source / "docs/flashing.md", public / "FLASH.md")
        shutil.copy2(source / "tools/make_settings.py", public / "make_settings.py")
        for name in ("LICENSE", "COPYING", "NOTICE.md"):
            shutil.copy2(source / name, public / name)
        shutil.copy2(build / "compatibility-evidence.txt", public)
        # Keep generated config with corresponding source, not in the firmware ZIP.
        shutil.copy2(example / "sdkconfig", example / "sdkconfig.release")
        sdk = Path(subprocess.check_output(
            ["bash", "-c", 'if ! command -v idf.py >/dev/null; then source "$IDF_PATH/export.sh" >/dev/null; fi; printf "%s" "$IDF_PATH"'],
            text=True,
        ).strip())
        # Include tracked SDK modifications, even mode-only changes, for provenance.
        sdk_patch = subprocess.check_output(["git", "-C", str(sdk), "diff", "HEAD", "--binary"])
        (source / "esp-idf-local.patch").write_bytes(sdk_patch)
        sdk_untracked = subprocess.check_output(
            ["git", "-C", str(sdk), "ls-files", "--others", "--exclude-standard"], text=True,
        ).strip()
        if sdk_untracked:
            raise RuntimeError("SDK has untracked files; review them before distributing firmware")
        archive(public, assets / "esp32s3-firmware.zip")
        # Full library and application source plus configuration/build scripts
        # allow modification and relinking without exposing machine-local debug
        # paths from ELF/object files, Ninja commands or CMake caches.
        archive_source(source, assets / "source.zip")
        sums = []
        for path in sorted(assets.glob("*.zip")):
            sums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")
        (assets / "SHA256SUMS").write_text("".join(sums))
        shutil.move(str(assets), output)
    print(f"Release assets ready: {output}")
    print("Publish both ZIPs and SHA256SUMS together; no upload was performed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="New output directory (default: dist/esp32s3-VERSION)")
    args = parser.parse_args()
    version = next(line.split('"')[1] for line in
                   (ROOT / "idf_component.yml").read_text().splitlines()
                   if line.startswith("version:"))
    output = (args.output or ROOT / "dist" / f"esp32s3-{version}").resolve()
    # Let build.sh activate ESP-IDF if IDF_PATH is set.
    package(output)


if __name__ == "__main__":
    main()
