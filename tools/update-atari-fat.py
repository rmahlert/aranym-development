#!/usr/bin/env python3
"""Replace existing files in an extracted Atari FAT16 filesystem image.

This deliberately small writer supports the large logical sector sizes used by
EasyMiNT images.  It only replaces existing 8.3 files and renames an existing
8.3 directory; it cannot create arbitrary directory entries.  ARAnyM IDE
images must be byte-swapped and their AHDI partition extracted first.
"""

import argparse
import datetime
import os
from pathlib import Path
import struct
import sys


class FatError(RuntimeError):
    pass


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def short_name_bytes(name):
    if name in {".", ".."}:
        stem, suffix = name, ""
    elif "." in name:
        stem, suffix = name.rsplit(".", 1)
    else:
        stem, suffix = name, ""
    try:
        stem_bytes = stem.upper().encode("cp437")
        suffix_bytes = suffix.upper().encode("cp437")
    except UnicodeEncodeError as error:
        raise FatError(f"not an 8.3 CP437 name: {name!r}") from error
    if not 1 <= len(stem_bytes) <= 8 or len(suffix_bytes) > 3:
        raise FatError(f"not an 8.3 name: {name!r}")
    forbidden = b'"*+,/:;<=>?[\\]|'
    if any(byte < 0x20 or byte in forbidden for byte in stem_bytes + suffix_bytes):
        raise FatError(f"invalid character in FAT name: {name!r}")
    return stem_bytes.ljust(8, b" ") + suffix_bytes.ljust(3, b" ")


def display_name(entry):
    raw = bytearray(entry[0:11])
    if raw[0] == 0x05:
        raw[0] = 0xe5
    stem = bytes(raw[:8]).decode("cp437").rstrip(" ")
    suffix = bytes(raw[8:11]).decode("cp437").rstrip(" ")
    return stem + (("." + suffix) if suffix else "")


def dos_timestamp(timestamp):
    value = datetime.datetime.fromtimestamp(timestamp)
    year = min(max(value.year, 1980), 2107)
    date = ((year - 1980) << 9) | (value.month << 5) | value.day
    time = (value.hour << 11) | (value.minute << 5) | (value.second // 2)
    return date, time


class Fat16Writer:
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
        self.first_root_sector = self.first_fat_sector + self.fat_count * self.sectors_per_fat
        self.first_data_sector = self.first_root_sector + self.root_sectors
        data_sectors = self.total_sectors - self.first_data_sector
        self.cluster_count = data_sectors // self.sectors_per_cluster
        if not 4085 <= self.cluster_count < 65525:
            raise FatError(f"expected FAT16, found {self.cluster_count} clusters")

        expected_size = self.total_sectors * self.bytes_per_sector
        actual_size = os.fstat(self.image.fileno()).st_size
        if actual_size < expected_size:
            raise FatError(f"truncated filesystem: expected {expected_size}, got {actual_size}")

        self.fat_size = self.sectors_per_fat * self.bytes_per_sector
        self.fat = bytearray(self.read_at(self.first_fat_sector * self.bytes_per_sector, self.fat_size))
        self.cluster_size = self.sectors_per_cluster * self.bytes_per_sector

    def read_at(self, offset, length):
        self.image.seek(offset)
        data = self.image.read(length)
        if len(data) != length:
            raise FatError(f"short read at byte {offset}")
        return data

    def write_at(self, offset, data):
        self.image.seek(offset)
        written = self.image.write(data)
        if written != len(data):
            raise FatError(f"short write at byte {offset}")

    def fat_value(self, cluster):
        return u16(self.fat, cluster * 2)

    def set_fat_value(self, cluster, value):
        struct.pack_into("<H", self.fat, cluster * 2, value)

    def cluster_offset(self, cluster):
        if cluster < 2 or cluster >= self.cluster_count + 2:
            raise FatError(f"invalid cluster {cluster}")
        sector = self.first_data_sector + (cluster - 2) * self.sectors_per_cluster
        return sector * self.bytes_per_sector

    def chain(self, first):
        if first == 0:
            return []
        result = []
        seen = set()
        cluster = first
        while cluster < 0xfff8:
            if cluster < 2 or cluster >= self.cluster_count + 2:
                raise FatError(f"invalid cluster {cluster} in chain starting at {first}")
            if cluster in seen:
                raise FatError(f"cluster loop at {cluster}")
            seen.add(cluster)
            result.append(cluster)
            cluster = self.fat_value(cluster)
            if cluster == 0xfff7:
                raise FatError(f"bad cluster in chain starting at {first}")
            if cluster == 0:
                raise FatError(f"free cluster in chain starting at {first}")
        return result

    def directory_entries(self, first_cluster):
        if first_cluster is None:
            base = self.first_root_sector * self.bytes_per_sector
            for index in range(self.root_entries):
                offset = base + index * 32
                yield offset, self.read_at(offset, 32)
            return
        for cluster in self.chain(first_cluster):
            base = self.cluster_offset(cluster)
            for local in range(0, self.cluster_size, 32):
                offset = base + local
                yield offset, self.read_at(offset, 32)

    def find_entry(self, first_cluster, name):
        wanted = short_name_bytes(name)
        for offset, entry in self.directory_entries(first_cluster):
            if entry[0] == 0x00:
                break
            if entry[0] == 0xe5 or entry[11] == 0x0f or entry[11] & 0x08:
                continue
            if entry[:11] == wanted:
                return offset, bytearray(entry)
        raise FatError(f"path component not found: {name}")

    def resolve(self, path):
        components = [part for part in path.replace("\\", "/").split("/") if part]
        if not components or any(part in {".", ".."} for part in components):
            raise FatError(f"invalid image path: {path!r}")
        directory = None
        for index, component in enumerate(components):
            offset, entry = self.find_entry(directory, component)
            if index + 1 < len(components):
                if not entry[11] & 0x10:
                    raise FatError(f"not a directory: {'/'.join(components[:index + 1])}")
                directory = u16(entry, 26)
        return offset, entry

    def rename_directory(self, old_path, new_name):
        offset, entry = self.resolve(old_path)
        if not entry[11] & 0x10:
            raise FatError(f"not a directory: {old_path}")
        parent = old_path.replace("\\", "/").rsplit("/", 1)[0] if "/" in old_path.replace("\\", "/") else ""
        new_path = f"{parent}/{new_name}" if parent else new_name
        try:
            self.resolve(new_path)
        except FatError:
            pass
        else:
            raise FatError(f"rename target already exists: {new_path}")
        old_name = display_name(entry)
        entry[:11] = short_name_bytes(new_name)
        self.write_at(offset, entry)
        return old_name, new_name

    def allocate(self, count):
        result = []
        for cluster in range(2, self.cluster_count + 2):
            if self.fat_value(cluster) == 0:
                result.append(cluster)
                if len(result) == count:
                    return result
        raise FatError(f"not enough free clusters (need {count}, found {len(result)})")

    def replace_file(self, image_path, source_path):
        offset, entry = self.resolve(image_path)
        if entry[11] & 0x10:
            raise FatError(f"not a file: {image_path}")
        data = source_path.read_bytes()
        old_size = u32(entry, 28)
        old_chain = self.chain(u16(entry, 26))
        required = (len(data) + self.cluster_size - 1) // self.cluster_size

        if required > len(old_chain):
            chain = old_chain + self.allocate(required - len(old_chain))
        else:
            chain = old_chain[:required]
        for cluster in old_chain[required:]:
            self.set_fat_value(cluster, 0)
        for index, cluster in enumerate(chain):
            next_cluster = chain[index + 1] if index + 1 < len(chain) else 0xffff
            self.set_fat_value(cluster, next_cluster)
            chunk = data[index * self.cluster_size:(index + 1) * self.cluster_size]
            self.write_at(self.cluster_offset(cluster), chunk.ljust(self.cluster_size, b"\0"))

        first = chain[0] if chain else 0
        struct.pack_into("<H", entry, 26, first)
        struct.pack_into("<I", entry, 28, len(data))
        date, time = dos_timestamp(source_path.stat().st_mtime)
        struct.pack_into("<H", entry, 22, time)
        struct.pack_into("<H", entry, 24, date)
        self.write_at(offset, entry)
        return old_size, len(data), len(old_chain), len(chain)

    def flush_fats(self):
        for index in range(self.fat_count):
            sector = self.first_fat_sector + index * self.sectors_per_fat
            self.write_at(sector * self.bytes_per_sector, self.fat)
        self.image.flush()
        os.fsync(self.image.fileno())


def parse_pair(value, description):
    if "=" not in value:
        raise argparse.ArgumentTypeError(f"{description} must contain '='")
    left, right = value.split("=", 1)
    if not left or not right:
        raise argparse.ArgumentTypeError(f"{description} must have values on both sides of '='")
    return left, right


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="extracted, byte-corrected FAT image to modify")
    parser.add_argument(
        "--rename-directory", action="append", default=[], metavar="OLD_PATH=NEW_NAME",
        help="rename an existing 8.3 directory (may be repeated)",
    )
    parser.add_argument(
        "--replace", action="append", default=[], metavar="IMAGE_PATH=HOST_FILE",
        help="replace an existing 8.3 file (may be repeated)",
    )
    args = parser.parse_args()
    if not args.rename_directory and not args.replace:
        parser.error("at least one --rename-directory or --replace is required")

    try:
        renames = [parse_pair(value, "rename") for value in args.rename_directory]
        replacements = [(path, Path(source)) for path, source in (
            parse_pair(value, "replacement") for value in args.replace
        )]
        missing = [str(source) for _, source in replacements if not source.is_file()]
        if missing:
            raise FatError("replacement source does not exist: " + ", ".join(missing))

        with args.image.open("r+b") as image:
            fat = Fat16Writer(image)
            # Validate every source path before changing the image.
            for old_path, _ in renames:
                fat.resolve(old_path)
            for image_path, _ in replacements:
                fat.resolve(image_path)

            for image_path, source in replacements:
                old_size, new_size, old_clusters, new_clusters = fat.replace_file(image_path, source)
                print(
                    f"replaced {image_path}: {old_size} -> {new_size} bytes "
                    f"({old_clusters} -> {new_clusters} clusters)"
                )
            # Rename last so replacement paths can use the original directory name.
            for old_path, new_name in renames:
                old_name, final_name = fat.rename_directory(old_path, new_name)
                print(f"renamed {old_path}: {old_name} -> {final_name}")
            fat.flush_fats()
    except (FatError, OSError, ValueError) as error:
        print(f"{parser.prog}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
