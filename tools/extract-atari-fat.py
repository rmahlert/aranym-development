#!/usr/bin/env python3
"""Extract an Atari-compatible FAT12/16 filesystem to a host directory.

This is intentionally read-only.  It handles logical sector sizes larger than
the host kernel or mtools accepts (EasyMiNT images commonly use 8192 bytes).
ARAnyM IDE images must be byte-swapped and their AHDI partition extracted
before this tool is used.
"""

import argparse
import datetime
import os
from pathlib import Path
import stat
import struct
import sys


EXECUTABLE_SUFFIXES = {
    ".acc", ".app", ".gtp", ".prg", ".sh", ".tos", ".ttp",
}


class FatError(RuntimeError):
    pass


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def dos_timestamp(date, time):
    if date == 0:
        return None
    try:
        value = datetime.datetime(
            1980 + ((date >> 9) & 0x7f),
            (date >> 5) & 0x0f,
            date & 0x1f,
            (time >> 11) & 0x1f,
            (time >> 5) & 0x3f,
            (time & 0x1f) * 2,
        )
        return value.timestamp()
    except ValueError:
        return None


class Fat16:
    def __init__(self, image):
        self.image = image
        boot = self.read_at(0, 512)
        self.bytes_per_sector = u16(boot, 11)
        self.sectors_per_cluster = boot[13]
        self.reserved_sectors = u16(boot, 14)
        self.fat_count = boot[16]
        self.root_entries = u16(boot, 17)
        total16 = u16(boot, 19)
        self.sectors_per_fat = u16(boot, 22)
        self.total_sectors = total16 or u32(boot, 32)

        if self.bytes_per_sector < 512 or self.bytes_per_sector & (self.bytes_per_sector - 1):
            raise FatError("invalid logical sector size")
        if not self.sectors_per_cluster or self.sectors_per_cluster & (self.sectors_per_cluster - 1):
            raise FatError("invalid sectors per cluster")
        if not self.fat_count or not self.sectors_per_fat or not self.root_entries:
            raise FatError("not a FAT12/16 filesystem")

        self.root_sectors = (
            self.root_entries * 32 + self.bytes_per_sector - 1
        ) // self.bytes_per_sector
        self.first_fat_sector = self.reserved_sectors
        self.first_root_sector = (
            self.first_fat_sector + self.fat_count * self.sectors_per_fat
        )
        self.first_data_sector = self.first_root_sector + self.root_sectors
        data_sectors = self.total_sectors - self.first_data_sector
        self.cluster_count = data_sectors // self.sectors_per_cluster
        if not 4085 <= self.cluster_count < 65525:
            raise FatError(
                f"expected FAT16, found filesystem with {self.cluster_count} clusters"
            )

        expected_size = self.total_sectors * self.bytes_per_sector
        actual_size = os.fstat(self.image.fileno()).st_size
        if actual_size < expected_size:
            raise FatError(
                f"truncated filesystem: expected {expected_size}, got {actual_size} bytes"
            )

        fat_size = self.sectors_per_fat * self.bytes_per_sector
        self.fat = self.read_at(self.first_fat_sector * self.bytes_per_sector, fat_size)
        self.cluster_size = self.sectors_per_cluster * self.bytes_per_sector
        self.file_count = 0
        self.directory_count = 0
        self.byte_count = 0

    def read_at(self, offset, length):
        self.image.seek(offset)
        data = self.image.read(length)
        if len(data) != length:
            raise FatError(f"short read at byte {offset}")
        return data

    def cluster_data(self, cluster):
        if cluster < 2 or cluster >= self.cluster_count + 2:
            raise FatError(f"invalid cluster {cluster}")
        sector = self.first_data_sector + (cluster - 2) * self.sectors_per_cluster
        return self.read_at(sector * self.bytes_per_sector, self.cluster_size)

    def chain(self, first):
        seen = set()
        cluster = first
        while cluster < 0xfff8:
            if cluster < 2 or cluster >= self.cluster_count + 2:
                raise FatError(f"invalid cluster {cluster} in chain starting at {first}")
            if cluster in seen:
                raise FatError(f"cluster loop at {cluster}")
            seen.add(cluster)
            yield cluster
            offset = cluster * 2
            if offset + 2 > len(self.fat):
                raise FatError(f"cluster {cluster} has no FAT entry")
            cluster = u16(self.fat, offset)
            if cluster == 0xfff7:
                raise FatError(f"bad cluster in chain starting at {first}")
            if cluster == 0:
                raise FatError(f"free cluster in chain starting at {first}")

    @staticmethod
    def short_name(entry):
        raw = bytearray(entry[0:11])
        if raw[0] == 0x05:
            raw[0] = 0xe5
        stem = bytes(raw[0:8]).decode("cp437").rstrip(" ")
        suffix = bytes(raw[8:11]).decode("cp437").rstrip(" ")
        return stem + (("." + suffix) if suffix else "")

    @staticmethod
    def safe_child(parent, name):
        if not name or name in {".", ".."} or "/" in name or "\\" in name:
            raise FatError(f"unsafe FAT pathname component {name!r}")
        child = parent / name
        if child.parent != parent:
            raise FatError(f"unsafe extraction path {child}")
        return child

    def directory_bytes(self, first_cluster):
        if first_cluster is None:
            size = self.root_entries * 32
            return self.read_at(self.first_root_sector * self.bytes_per_sector, size)
        return b"".join(self.cluster_data(cluster) for cluster in self.chain(first_cluster))

    def extract_directory(self, first_cluster, destination):
        entries = self.directory_bytes(first_cluster)
        directory_times = []
        for offset in range(0, len(entries), 32):
            entry = entries[offset:offset + 32]
            if len(entry) < 32 or entry[0] == 0x00:
                break
            if entry[0] == 0xe5:
                continue
            attributes = entry[11]
            if attributes == 0x0f or attributes & 0x08:
                continue

            name = self.short_name(entry)
            if name in {".", ".."}:
                continue
            target = self.safe_child(destination, name)
            first = u16(entry, 26)
            modified = dos_timestamp(u16(entry, 24), u16(entry, 22))

            if attributes & 0x10:
                target.mkdir(exist_ok=False)
                self.directory_count += 1
                self.extract_directory(first, target)
                directory_times.append((target, modified))
                continue

            size = u32(entry, 28)
            remaining = size
            with target.open("xb") as output:
                if remaining:
                    for cluster in self.chain(first):
                        data = self.cluster_data(cluster)
                        chunk = data[:remaining]
                        output.write(chunk)
                        remaining -= len(chunk)
                        if not remaining:
                            break
            if remaining:
                raise FatError(f"cluster chain for {target} is {remaining} bytes short")
            mode = 0o755 if target.suffix.lower() in EXECUTABLE_SUFFIXES else 0o644
            target.chmod(mode)
            if modified is not None:
                os.utime(target, (modified, modified))
            self.file_count += 1
            self.byte_count += size

        for directory, modified in directory_times:
            if modified is not None:
                os.utime(directory, (modified, modified))

    def extract(self, destination):
        destination.mkdir(parents=True, exist_ok=False)
        self.extract_directory(None, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="extracted, byte-corrected FAT image")
    parser.add_argument("destination", type=Path, help="new output directory")
    args = parser.parse_args()

    if args.destination.exists():
        parser.error(f"destination already exists: {args.destination}")

    try:
        with args.image.open("rb") as image:
            fat = Fat16(image)
            fat.extract(args.destination)
    except (FatError, OSError) as error:
        print(f"{parser.prog}: {error}", file=sys.stderr)
        return 1

    print(
        f"extracted {fat.file_count} files ({fat.byte_count} bytes) and "
        f"{fat.directory_count} directories to {args.destination}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
