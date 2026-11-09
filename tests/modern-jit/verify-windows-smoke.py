#!/usr/bin/env python3
"""Independent static packet/ZIP checks, including tamper negative controls.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import zipfile

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def verify(folder):
    manifest = json.loads((folder/'SHA256-MANIFEST.json').read_text())
    actual = {x.relative_to(folder).as_posix() for x in folder.rglob('*') if x.is_file()}
    expected = set(manifest['files']) | {'SHA256-MANIFEST.json'}
    if actual != expected:
        raise ValueError('unexpected or missing file')
    for name, expected_hash in manifest['files'].items():
        if Path(name).is_absolute() or '..' in Path(name).parts or sha(folder/name) != expected_hash:
            raise ValueError('hash/path mismatch')
    if any(x.is_symlink() or x.name == '.git' or x.suffix.lower() in ('.img', '.rom', '.ttf', '.otf', '.prg', '.st', '.tos', '.sys', '.bundle') for x in folder.rglob('*')):
        raise ValueError('forbidden guest/history/symlink export')
    seeds = json.loads((folder/'SEEDS.json').read_text())
    if not all(re.fullmatch('[a-f0-9]{64}', value) for value in seeds['files'].values()) or seeds['assets_included']:
        raise ValueError('bad seed metadata')
    closure = json.loads((folder/'DEPENDENCIES.json').read_text())
    for item in closure['files'].values():
        if sha(folder/item['file']) != item['sha256']:
            raise ValueError('dependency digest mismatch')
        if item.get('source_sha256', item['sha256']) != item['sha256']:
            raise ValueError('dependency differs from matched source')
    identity = json.loads((folder/'SOURCE-IDENTITY.json').read_text())
    if identity['functional_base_commit'] != 'fe250a4ac8828fa990e438e8cf241035cbd953f5' or identity['memory_pilot_included'] or identity['launched'] or identity['installed']:
        raise ValueError('unexpected source/runtime claim')
    script = (folder/'windows-smoke.ps1').read_text()
    if 'ExecutionPolicy' in script or 'Unblock-File' in script or 'Invoke-Expression' in script:
        raise ValueError('unexpected execution-policy/loader operation')
    return {'files': len(actual), 'dlls': len(closure['files'])-1, 'manifest_sha256': sha(folder/'SHA256-MANIFEST.json')}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packet', type=Path)
    parser.add_argument('--negative-controls', action='store_true')
    a = parser.parse_args()
    packet = a.packet.resolve(strict=True)
    result = verify(packet)
    archive = packet.with_suffix('.zip')
    with zipfile.ZipFile(archive) as package:
        names = {x.filename for x in package.infolist()}
        expected = {packet.name+'/'+x.relative_to(packet).as_posix() for x in packet.rglob('*') if x.is_file()}
        if names != expected or len(names) != len(package.infolist()) or package.testzip() is not None:
            raise ValueError('ZIP entries/CRC differ')
        for name in names:
            if hashlib.sha256(package.read(name)).hexdigest() != sha(packet/name.split('/', 1)[1]):
                raise ValueError('ZIP content differs')
    result.update(zip_sha256=sha(archive), zip_matches_frozen_folder=True, target_launched=False, powershell_parser_available=False)
    if a.negative_controls:
        negatives = {}
        with tempfile.TemporaryDirectory(prefix='aranym-jit-packet-negative-') as temporary:
            for kind in ('changed-dll', 'missing-dll', 'extra-dll'):
                copy = Path(temporary)/kind
                shutil.copytree(packet, copy)
                dll = sorted(copy.glob('*.dll'))[0]
                if kind == 'changed-dll':
                    with dll.open('ab') as stream:
                        stream.write(b'negative-control')
                elif kind == 'missing-dll':
                    dll.unlink()
                else:
                    (copy/'unexpected.dll').write_bytes(b'negative-control')
                try:
                    verify(copy)
                except ValueError as error:
                    negatives[kind] = {'rejected': True, 'reason': str(error)}
                else:
                    raise AssertionError('negative control accepted: '+kind)
        result['negative_controls'] = negatives
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
