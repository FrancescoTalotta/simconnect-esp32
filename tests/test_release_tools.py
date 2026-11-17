#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


settings = load("make_settings")
release = load("package_release")


class ReleaseTests(unittest.TestCase):
    def test_source_archive_excludes_generated_local_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            for name in ("sdkconfig", "sdkconfig.old", "app_config.h", "dependencies.lock",
                         "sdkconfig.release", "app_main.c"):
                (source / name).write_text("test fixture")
            (source / "build").mkdir()
            (source / "build" / "private.bin").write_bytes(b"private test fixture")
            output = Path(directory) / "source.zip"
            release.archive_source(source, output)
            with zipfile.ZipFile(output) as zipped:
                self.assertEqual(set(zipped.namelist()), {"sdkconfig.release", "app_main.c"})

    def validate(self, **changes):
        values = dict(ssid="bench", password="test,password", host="192.168.1.100",
                      port="5111", aircraft="FlyByWire", gpio=38, brightness=16)
        values.update(changes)
        settings.validate(**values)

    def test_valid_settings(self):
        self.validate()
        self.validate(ssid="é" * 16, password="", gpio=48, brightness=0)
        self.validate(password="a" * 64, port="65535", brightness=255)

    def test_invalid_settings(self):
        for changes in (
            dict(ssid=""), dict(ssid="é" * 17), dict(ssid="a\0b"),
            dict(password="short"), dict(password="g" * 64),
            dict(password="a" * 65), dict(password="a\0bbbbbb"),
            dict(host="localhost"), dict(host="256.1.1.1"),
            dict(port="0"), dict(port="65536"), dict(port="+5111"),
            dict(port="５１１１"), dict(aircraft=""), dict(aircraft="a" * 256),
            dict(gpio=47), dict(brightness=-1), dict(brightness=256),
        ):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.validate(**changes)

    def test_refuses_to_overwrite_private_image(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "user-settings.bin"
            output.write_bytes(b"keep")
            with self.assertRaises(ValueError):
                settings.generate(output, "bench", "", "192.168.1.100", "5111",
                                  "FlyByWire", 38, 16)
            self.assertEqual(output.read_bytes(), b"keep")

    def test_source_snapshot_excludes_local_credentials_and_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            release.copy_source(destination)
            paths = [p.relative_to(destination) for p in destination.rglob("*")]
            self.assertTrue((destination / "src/simconnect_client.c").is_file())
            self.assertTrue((destination / "tests/test_client.py").is_file())
            self.assertTrue((destination / "examples/fbw_external_power/main/app_config.h.example").is_file())
            for path in paths:
                self.assertFalse(set(path.parts) & release.EXCLUDED_DIRS)
                self.assertNotIn(path.name, release.EXCLUDED_FILES)


if __name__ == "__main__":
    unittest.main()
