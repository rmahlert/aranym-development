#!/usr/bin/env python3
"""Freeze a private smoke packet by static PE inspection; never launch targets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BINARY = 'aranym-upstream-jit-experimental.exe'
EXPECTED = 'f3eac0a6e7abafe42eb7f7f12f8e1f20b7b328cef1ecd7bfe54449fe001bbae7'
BASE = 'fe250a4ac8828fa990e438e8cf241035cbd953f5'
PIN = 'd82bc2549f27943d11dc59482d5388daf1c0f337'
SYSTEM = set('advapi32 kernel32 user32 gdi32 shell32 shlwapi ole32 oleaut32 ws2_32 wsock32 iphlpapi msvcrt winmm imm32 version comdlg32 comctl32 setupapi rpcrt4 dwmapi uxtheme crypt32 secur32 bcrypt ntdll opengl32 glu32 dnsapi mswsock hid usp10 d3d9 dsound dxgi avicap32'.split())
SEEDS = {
    'prepared.img': '262bd6e8cad210b650e335e3c3724f28a201ca151fc6630f7649882ba607e8fb',
    'emutos-aranym.img': 'b723d04ae9912b8e5fdcb9bd2dffacb3d57968ab3e543b7ace5e687a04bef63b',
    'starting-nvram.bin': '9b2d79a0cf40507aa66c12803d57cd536bc83e3df549da66fbed85fccb91955a',
    'host/FONTS/DVSANS.TTF': 'b4c632e3cdf9acc7f28758fb5a323c8524d7fc6660d46904d9b6cbe2809c419c',
    'host/FONTS/LIBMONO.TTF': 'd18235f1e874771f4eaec3ddd6ddb4db849d334bdc6930c5c2185d9344834e62',
    'host/FONTS/LIBSANS.TTF': '075e71be4c58beda172ecb574a799308f04c3d054c6a5b7434954d3b0f1b28d8',
}
MARKERS = ['MODERN040_EXPERIMENTAL source=d82bc254', '(2be9839adc) now installed.',
           'CPU FONT ok AES ready', 'CPU FONT ok resident fVDI cookie present',
           'CPU FONT vq_vgdos=66564449', 'CPU FONT planes=00000020',
           'CPU FONT loaded-fonts=00000003', 'CPU FONT first-vector-id=0000163d',
           'CPU FONT ok font point metrics', 'CPU FONT ok font text extent',
           'CPU FONT actual VDI text call returned',
           'CPU FONT SERVICES PASS; requesting GEMDOS Shutdown(0)']

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def dump(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')

def inspect(path):
    result = subprocess.check_output(['x86_64-w64-mingw32-objdump', '-p', str(path)], text=True)
    if 'file format pei-x86-64' not in result:
        raise RuntimeError(f'not an AMD64 PE: {path}')
    return re.findall(r'DLL Name:\s+(\S+)', result)

def system(name):
    lower = name.lower()
    return lower.removesuffix('.dll') in SYSTEM or lower == 'winspool.drv' or lower.startswith(('api-ms-win-', 'ext-ms-win-'))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'mingw-root', 'jpeg-root', 'upstream', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    a = parser.parse_args()
    output = a.output.resolve()
    if output.exists() or output.with_suffix('.zip').exists():
        raise RuntimeError('choose a new frozen output/zip path')
    candidate = a.candidate.resolve(strict=True)
    qualification = json.loads((candidate/'qualification.json').read_text())
    if qualification['functional_base_commit'] != BASE or qualification['memory_pilot_included'] or qualification['launched'] or sha(candidate/BINARY) != EXPECTED:
        raise RuntimeError('unexpected candidate provenance')
    for path, expected in json.loads((candidate/'windows-jit-build.json').read_text())['source_sha256'].items():
        if sha(ROOT/path) != expected:
            raise RuntimeError(f'functional source differs from frozen base: {path}')
    if not all(re.fullmatch('[0-9a-f]{64}', value) for value in SEEDS.values()):
        raise RuntimeError('malformed seed hash')
    output.mkdir(parents=True)
    shutil.copyfile(candidate/BINARY, output/BINARY)
    search = [a.mingw_root/'bin', a.mingw_root/'lib', a.jpeg_root/'bin']
    closure = {}; pending = [output/BINARY]
    while pending:
        current = pending.pop(0)
        dependencies = inspect(current)
        record = closure.setdefault(current.name.lower(), {'file': current.name, 'imports': dependencies})
        record['sha256'] = sha(current)
        record['imports'] = dependencies
        for name in dependencies:
            if system(name) or name.lower() in closure:
                continue
            found = next((p/name for p in search if (p/name).is_file()), None)
            if found is None:
                compiler_file = Path(subprocess.check_output(['x86_64-w64-mingw32-g++', '-print-file-name='+name], text=True).strip())
                if compiler_file.is_file():
                    found = compiler_file
            if found is None:
                raise RuntimeError('unresolved DLL: '+name)
            found = found.resolve(strict=True)
            target = output/name
            if target.exists():
                if sha(target) != sha(found):
                    raise RuntimeError('ambiguous DLL: '+name)
                continue
            shutil.copyfile(found, target)
            closure[name.lower()] = {'file': name, 'source': str(found), 'source_sha256': sha(found)}
            pending.append(target)
    for entry in closure.values():
        for name in entry['imports']:
            if not system(name) and name.lower() not in closure:
                raise RuntimeError('incomplete import closure')
    dump(output/'DEPENDENCIES.json', {'static_inspection_only': True, 'closure_complete': True, 'files': closure})
    licenses = output/'licenses'; licenses.mkdir()
    for name in ('COPYING', 'AUTHORS'):
        shutil.copyfile(ROOT/name, licenses/(name+'.ARAnyM'))
    shutil.copyfile(a.upstream/'COPYING', licenses/'COPYING.WinUAE')
    for prefix in (a.mingw_root, a.jpeg_root):
        for source in sorted((prefix/'share/licenses').iterdir()):
            if (licenses/source.name).exists():
                raise RuntimeError('duplicate license directory')
            shutil.copytree(source, licenses/source.name)
    runtime = licenses/'Debian-runtime'; runtime.mkdir()
    for package in ('gcc-mingw-w64-base', 'gcc-mingw-w64-x86-64-win32-runtime', 'mingw-w64-common', 'gcc-13-base'):
        shutil.copyfile(Path('/usr/share/doc')/package/'copyright', runtime/(package+'.copyright'))
    for name in ('GPL-2', 'GPL-3', 'LGPL-2.1', 'LGPL-3'):
        shutil.copyfile(Path('/usr/share/common-licenses')/name, runtime/name)
    for name in ('windows-jit-build.json', 'windows-jit-static-review.json', 'qualification.json'):
        shutil.copyfile(candidate/name, output/name)
    for name in ('windows-smoke.ps1', 'WINDOWS-SMOKE.txt'):
        shutil.copyfile(HERE/name, output/name)
    dump(output/'SEEDS.json', {'files': SEEDS, 'required_markers': MARKERS, 'assets_included': False, 'legacy_fpu': True})
    functional = json.loads((candidate/'windows-jit-build.json').read_text())
    dump(output/'SOURCE-IDENTITY.json', {
        'functional_base_commit': BASE, 'candidate_metadata_commit': qualification['metadata_commit'],
        'upstream_pin': PIN, 'source_at_packaging': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'memory_pilot_included': False, 'functional_source_sha256': functional['source_sha256'],
        'package_recipe_sha256': sha(Path(__file__)), 'smoke_script_sha256': sha(HERE/'windows-smoke.ps1'),
        'launched': False, 'installed': False, 'runtime_acceptance': 'pending',
        'compiled_profile': '%USERPROFILE%\\aranym-upstream-jit-experimental',
        'source_availability': 'Owned complete source and incremental bundles remain with root; this private review packet includes no repository/history bundle.'})
    for path in output.rglob('*'):
        if path.is_symlink() or path.name == '.git' or path.suffix.lower() in ('.img', '.rom', '.ttf', '.otf', '.prg', '.st', '.tos', '.sys', '.bundle'):
            raise RuntimeError('forbidden export asset: '+str(path))
    manifest = {'frozen': True, 'launched': False, 'binary': BINARY, 'binary_sha256': EXPECTED,
                'functional_base_commit': BASE, 'memory_pilot_included': False,
                'files': {p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob('*')) if p.is_file()}}
    dump(output/'SHA256-MANIFEST.json', manifest)
    archive = output.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as package:
        for path in sorted(output.rglob('*')):
            if path.is_file():
                package.write(path, (Path(output.name)/path.relative_to(output)).as_posix())
    print(json.dumps({'packet': str(output), 'archive': str(archive), 'zip_sha256': sha(archive),
                      'manifest_sha256': sha(output/'SHA256-MANIFEST.json'), 'dlls': len(closure)-1,
                      'files': len(manifest['files'])+1, 'launched': False}, indent=2))

if __name__ == '__main__':
    main()
