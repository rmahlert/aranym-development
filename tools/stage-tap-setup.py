#!/usr/bin/env python3
"""Validate and stage the shared standalone TAP setup kit beside nfeth.xif."""
import hashlib
from pathlib import Path, PurePosixPath
import stat
import sys
import zipfile

archive, destination = map(Path, sys.argv[1:])
checksum = archive.with_name(archive.name + '.sha256').read_text().split()
if len(checksum) != 2 or checksum[1] != archive.name or hashlib.sha256(archive.read_bytes()).hexdigest() != checksum[0]:
    raise SystemExit('TAP setup archive checksum/basename mismatch')
with zipfile.ZipFile(archive) as source:
    names = set()
    root = None
    files = {}
    for member in source.infolist():
        path = PurePosixPath(member.filename)
        if path.is_absolute() or '..' in path.parts or len(path.parts) < 2 or stat.S_ISLNK(member.external_attr >> 16):
            raise SystemExit('Unsafe TAP setup archive path')
        if root is None: root = path.parts[0]
        if path.parts[0] != root or not root.startswith('Atari-TAP-Setup-') or member.is_dir():
            raise SystemExit('Unexpected TAP setup archive layout')
        relative = path.relative_to(root).as_posix()
        if relative in names: raise SystemExit('Duplicate TAP setup archive path')
        names.add(relative)
        files[relative] = source.read(member)
    required = {'Atari-TAP-Setup.cmd', 'tap-adapter.ps1', 'tap-setup.ps1', 'install-tap.exe',
                'Collect-WiFi-LAN.cmd', 'collect-wifi.ps1', 'tap/tap0901.sys', 'tap/tap0901.cat',
                'tap/OemVista.inf', 'source/install-tap.c', 'CONTENTS.sha256'}
    if not required <= names: raise SystemExit('Incomplete TAP setup kit')
    if files['install-tap.exe'][:2] != b'MZ': raise SystemExit('Invalid TAP installer executable')
    manifest = {}
    for line in files['CONTENTS.sha256'].decode('ascii').splitlines():
        digest, name = line.split('  ', 1)
        if name in manifest: raise SystemExit('Duplicate manifest entry')
        manifest[name] = digest
    if set(manifest) != names - {'CONTENTS.sha256'}: raise SystemExit('Incomplete TAP setup manifest')
    for name, digest in manifest.items():
        if hashlib.sha256(files[name]).hexdigest() != digest: raise SystemExit('TAP setup payload checksum mismatch')
    if any((destination/name).exists() for name in names): raise SystemExit('Refusing to overwrite network setup files')
    for name, data in files.items():
        path = destination/name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as output: output.write(data)
print('Staged verified shared TAP setup:', archive.name)
