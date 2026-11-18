"""Verify and stage the pinned MIDI dependency for application packages.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
from pathlib import Path
import shutil

PIN = "8341a6713842d2374c3aad8998d43b20ad0db435"
VERSION = "2.0.7"
LICENSE_SHA256 = "8d187c40c782da0e24489c4c4bf7590635e072dc0b70b0afda7b0652d290c1ea"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class PortMidiDistribution:
    def __init__(self, prefix):
        self.prefix = Path(prefix).resolve(strict=True)
        self.record = json.loads((self.prefix / "provenance.json").read_text())
        if self.record.get("source_pin") != PIN or self.record.get("version") != VERSION:
            raise RuntimeError("PortMidi build does not match the required source pin")
        for name, digest in self.record["files"].items():
            path = (self.prefix / name).resolve(strict=True)
            if not path.is_relative_to(self.prefix) or sha(path) != digest:
                raise RuntimeError("PortMidi installed file mismatch: " + name)
        self.source = self.checked_file(self.record["source_archive"])
        self.license = self.checked_file(self.record["license"])
        if sha(self.license) != LICENSE_SHA256:
            raise RuntimeError("PortMidi license differs from the pinned source")

    def checked_file(self, item):
        path = (self.prefix / item["path"]).resolve(strict=True)
        if not path.is_relative_to(self.prefix) or sha(path) != item["sha256"]:
            raise RuntimeError("PortMidi source/license integrity mismatch")
        return path

    def library(self, platform):
        candidates = {
            "linux": self.prefix / "lib/libportmidi.so.2",
            "darwin": self.prefix / "lib/libportmidi.2.dylib",
            "windows": self.prefix / "bin/libportmidi.dll",
        }
        path = candidates[platform].resolve(strict=True)
        if not path.is_relative_to(self.prefix):
            raise RuntimeError("PortMidi library escapes its verified prefix")
        name = str(path.relative_to(self.prefix))
        if self.record["files"].get(name) != sha(path):
            raise RuntimeError("PortMidi library is not in the verified build manifest")
        return path

    def stage_notices(self, license_directory, source_directory):
        license_directory, source_directory = Path(license_directory), Path(source_directory)
        license_directory.mkdir(parents=True, exist_ok=True)
        source_directory.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.license, license_directory / "PortMidi-LICENSE.txt")
        shutil.copy2(self.source, source_directory / self.source.name)
        (license_directory / "PortMidi-BUILD.json").write_text(json.dumps(self.summary(), indent=2) + "\n")

    def summary(self):
        return {key: self.record[key] for key in
                ("version", "source_pin", "target", "license", "source_archive")}
