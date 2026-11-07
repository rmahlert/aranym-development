#!/usr/bin/env python3
"""Stage a separate installer system from release files and a verified image."""
import argparse
import configparser
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--image-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.image.open('rb') as file:
        if hashlib.file_digest(file, 'sha256').hexdigest() != args.image_sha256:
            parser.error('updated image checksum mismatch')
    args.output.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(args.bundle) as archive:
        for item in archive.infolist():
            name = PurePosixPath(item.filename)
            if name.is_absolute() or '..' in name.parts or '\\' in item.filename:
                parser.error('unsafe release archive path')
            relative = name.relative_to('easyaramint')
            if item.is_dir() or not relative.parts:
                continue
            if relative.parts[0] not in ('config', 'emutos-aranym.img', 'host_fs', 'nfeth.xif'):
                continue
            target = args.output.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(item) as source, target.open('xb') as dest:
                shutil.copyfileobj(source, dest)
    (args.output / 'EasyMint').mkdir()
    shutil.copyfile(args.image, args.output / 'EasyMint/aranym-ide-1.57G.img')
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(args.output / 'config')
    for key, value in {'EmuTOS': 'emutos-aranym.img', 'Floppy': '', 'TOS': '', 'Bootstrap': ''}.items():
        config['GLOBAL'][key] = value
    config['IDE0']['Path'] = 'EasyMint/aranym-ide-1.57G.img'
    config['HOSTFS']['I'] = ''
    config['ETH0'].update(Type='slirp', HostIP='10.0.2.2', AtariIP='10.0.2.15', Netmask='255.255.255.0')
    config['HOTKEYS'].update(Screenshot='PrintScreen', Setup='Pause')
    with (args.output / 'config').open('w', newline='\r\n') as file:
        config.write(file)


if __name__ == '__main__':
    main()
