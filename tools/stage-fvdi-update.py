#!/usr/bin/env python3
"""Verify and stage the corresponding fVDI binaries, sources and licenses."""
import hashlib
from pathlib import Path, PurePosixPath
import sys
import zipfile


def stage(archive, destination):
    expected = Path(str(archive) + '.sha256').read_text().split()[0]
    with archive.open('rb') as file:
        if hashlib.file_digest(file, 'sha256').hexdigest() != expected:
            raise ValueError('fVDI archive checksum mismatch')
    with zipfile.ZipFile(archive) as package:
        entries = [item for item in package.infolist() if not item.is_dir()]
        names = [PurePosixPath(item.filename) for item in entries]
        if any('\\' in str(name) or ':' in str(name) or name.is_absolute() or '..' in name.parts or len(name.parts) < 2 for name in names):
            raise ValueError('unsafe fVDI archive path')
        if len({name.parts[0] for name in names}) != 1:
            raise ValueError('expected one fVDI package directory')
        prefix = names[0].parts[0] + '/'
        if len(set(item.filename for item in entries)) != len(entries):
            raise ValueError('duplicate fVDI archive entry')
        checksums = {}
        for line in package.read(prefix + 'SHA256SUMS.txt').decode().splitlines():
            digest, name = line.split('  ', 1)
            checksums[name] = digest
        expected_files = set(checksums) | {'SHA256SUMS.txt'}
        if {str(name.relative_to(prefix)) for name in names} != expected_files:
            raise ValueError('fVDI archive has unlisted files')
        required = {'AUTO/FVDI.PRG', 'MINT/BIN/FVDI.PRG', 'GEMSYS/ARANYM.SYS'}
        if not required <= set(checksums):
            raise ValueError('missing fVDI binary')
        for name, digest in checksums.items():
            if hashlib.sha256(package.read(prefix + name)).hexdigest() != digest:
                raise ValueError('fVDI payload checksum mismatch: ' + name)
        for item in entries:
            relative = PurePosixPath(item.filename).relative_to(prefix)
            target = destination.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as output:
                output.write(package.read(item))


if __name__ == '__main__':
    stage(Path(sys.argv[1]), Path(sys.argv[2]))
