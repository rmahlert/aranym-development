#!/usr/bin/env python3
"""Run the closed licensed MiNT/fVDI font fixture in fresh owned copies.
Never launch Windows, install binaries or use a live owner image.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path
PREPARED='262bd6e8cad210b650e335e3c3724f28a201ca151fc6630f7649882ba607e8fb'
def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build-dir','prepared-image','rom','host-dir','nvram','work-dir'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--jit',action='store_true');p.add_argument('--checked-move',action='store_true');p.add_argument('--timeout',type=int,default=60)
    a=p.parse_args();build=a.build_dir.resolve(strict=True)
    identity=json.loads((build/'upstream-jit-build.json').read_text())
    if not identity.get('modern_core'):raise RuntimeError('requires explicit combined modern-core build')
    binary=Path(identity['binary']).resolve(strict=True);profile=Path(identity['profile']).resolve(strict=True)
    if profile==Path.home() or build.parent not in profile.parents:raise RuntimeError('requires a build-local owned profile')
    image=a.prepared_image.resolve(strict=True)
    if sha(image)!=PREPARED:raise RuntimeError('input image differs from the closed accepted c1 fixture')
    work=a.work_dir.resolve()
    if work.exists() and any(work.iterdir()):raise RuntimeError('choose a new owned work directory for each arm')
    if not 1<=a.timeout<=300:raise RuntimeError('fixture deadline must be1..300 seconds')
    work.mkdir(parents=True,exist_ok=True)
    (work/'owned-system-fixture.json').write_text('{"purpose":"closed c1 MiNT/fVDI fixture"}\n')
    subprocess.run(['cp','-f','--reflink=auto','--sparse=always',str(image),str(work/'boot.img')],check=True)
    if sha(work/'boot.img')!=PREPARED:raise RuntimeError('copied starting image differs')
    shutil.copytree(a.host_dir.resolve(strict=True),work/'host')
    shutil.copyfile(a.nvram.resolve(strict=True),work/'starting-nvram.bin')
    shutil.copyfile(work/'starting-nvram.bin',profile/'nvram')
    rom=a.rom.resolve(strict=True);config=work/'config'
    config.write_text(f'''[GLOBAL]
FastRAM = 64
FixedMemoryOffset = 60000000
EmuTOS = {rom}
[JIT]
JIT = {"Yes" if a.jit else "No"}
JITFPU = No
JITCacheSize = 8192
[HOSTFS]
H = {work/'host'}
[IDE0]
Present = Yes
Path = {work/'boot.img'}
ByteSwap = No
ReadOnly = No
ModelName = Master
[ETH0]
Type =
Tunnel =
[STARTUP]
GrabMouse = No
[AUDIO]
Enabled = No
[TOS]
RedirConsole = Yes
[NFVDI]
UseHostMouseCursor = No
''')
    env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',ARANYM_MODERN040_LEGACY_FPU='1',ARANYM_UPSTREAM_JIT_DIAGNOSTICS='1')
    for name in ('ARANYM_UPSTREAM_JIT_FALLBACK_ONLY','ARANYM_UPSTREAM_JIT_GROUP','ARANYM_UPSTREAM_JIT_CHECKED_MOVE','ARANYM_UPSTREAM_JIT_SIDE_EFFECT_FIXTURE','ARANYM_MODERN040_DIAGNOSTICS'):env.pop(name,None)
    if a.checked_move:
        if not a.jit:raise RuntimeError('checked memory pilot requires JIT enabled')
        env['ARANYM_UPSTREAM_JIT_CHECKED_MOVE']='1'
    log=work/'run.log';started=time.monotonic()
    with log.open('w') as output:
        result=subprocess.run(['timeout','-k','5',str(a.timeout),str(binary),'-c',str(config)],cwd=work,env=env,stdout=output,stderr=subprocess.STDOUT)
    seconds=time.monotonic()-started;text=log.read_text(errors='replace')
    required=['MODERN040_EXPERIMENTAL source=d82bc254','(2be9839adc) now installed.',
              'CPU FONT ok AES ready','CPU FONT ok resident fVDI cookie present',
              'CPU FONT vq_vgdos=66564449','CPU FONT planes=00000020','CPU FONT loaded-fonts=00000003',
              'CPU FONT first-vector-id=0000163d','CPU FONT ok font point metrics','CPU FONT ok font text extent',
              'CPU FONT actual VDI text call returned','CPU FONT SERVICES PASS; requesting GEMDOS Shutdown(0)']
    checks={marker:marker in text for marker in required}
    witness=re.search(r'UPSTREAM-JIT compiled-insns=(\d+) cached-dispatches=(\d+) executed-translated=(\d+) executed-guest=(\d+)',text)
    observed={} if not witness else dict(zip(('emitted_instructions','cached_dispatches','executed_translated','executed_guest'),map(int,witness.groups())))
    memory_witness=re.search(r'executed-memory=(\d+)',text)
    if memory_witness:observed['executed_memory']=int(memory_witness.group(1))
    record={'jit':a.jit,'checked_move':a.checked_move,'legacy_fpu':True,'exit':result.returncode,'seconds':seconds,'checks':checks,
            'jit_observed':observed,'binary_sha256':sha(binary),'build_identity_sha256':sha(build/'upstream-jit-build.json'),
            'source_image_sha256':PREPARED,'starting_image_sha256':PREPARED,'config_sha256':sha(config),
            'rom_sha256':sha(rom),'nvram_pre_sha256':sha(work/'starting-nvram.bin'),'nvram_post_sha256':sha(profile/'nvram'),
            'host_input_sha256':{str(x.relative_to(work/'host')):sha(x) for x in sorted((work/'host').rglob('*')) if x.is_file()},
            'log_sha256':sha(log),'passed':result.returncode==0 and all(checks.values()) and (not a.jit or observed.get('executed_guest',0)>0) and (not a.checked_move or observed.get('executed_memory',0)>0)}
    (work/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
    return 0 if record['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
