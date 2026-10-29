#!/usr/bin/env python3
"""nights-nx (2026-10-29): the package of step 7, from a Switch build folder.

  dist/package/nights-nx_<stamp>/
    COPY_TO_SD_switch_nights-nx/   -> sdmc:/switch/nights-nx/ (beside it: game_root/ with the game's extracted package)
        nights-nx.nro, renut.toml (shipped), renut_play.toml (diagnostics off), renut_ab.toml (in-run A/B),
        configs/ (the picture options for the user), deko3d_shaders.bin + pipeline_list.bin (the emulator's caches)
    elf/renut_<md5 of the NRO>.elf, renut.map   (a console profile's addresses -> functions and lines)
    source/nights-nx_src_<commit>.zip            (git archive of the commit the NRO was built from)
    source/generated_<commit>.zip                (app/generated: DERIVED FROM YOUR GAME, keep it local)
    PACKAGE_NOTES.md                             (docs/package-notes.md with the build's facts filled in)

  python -I tools/make_nights_package.py [--build app/out/switch-pgo-use] [--stamp 2026-10-29_0500]
"""
import argparse
import datetime
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = 'D:/New folder/ryu-data/nights/sdcard/switch/nights-nx'
FORWARDER_ID = '0500004E49474854'


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', default=os.path.join(ROOT, 'app', 'out', 'switch-pgo-use'))
    ap.add_argument('--stamp', default=datetime.datetime.now().strftime('%Y-%m-%d_%H%M'))
    a = ap.parse_args()
    nro, elf, cmap = (os.path.join(a.build, n) for n in ('renut.nro', 'renut', 'renut.map'))
    for p in (nro, elf):
        if not os.path.isfile(p):
            sys.exit(f'make_nights_package: {p} missing')
    commit = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short=8', 'HEAD'], check=True, capture_output=True,
                            text=True).stdout.strip()
    dirty = subprocess.run(['git', '-C', ROOT, 'status', '--porcelain', '--', 'app/src', 'sdk', 'app/CMakeLists.txt'],
                           check=True, capture_output=True, text=True).stdout.strip()
    out = os.path.join(ROOT, 'dist', 'package', f'nights-nx_{a.stamp}')
    sd = os.path.join(out, 'COPY_TO_SD_switch_nights-nx')
    os.makedirs(os.path.join(sd, 'configs'), exist_ok=True)
    shutil.copy2(nro, os.path.join(sd, 'nights-nx.nro'))
    dist = os.path.join(ROOT, 'dist', 'switch', 'nights-nx')
    for n in ('renut.toml',):
        shutil.copy2(os.path.join(dist, n), os.path.join(sd, n))
    for n in sorted(os.listdir(os.path.join(dist, 'configs'))):
        shutil.copy2(os.path.join(dist, 'configs', n), os.path.join(sd, 'configs', n))
    caches = []
    for n in ('deko3d_shaders.bin', 'pipeline_list.bin'):
        if os.path.isfile(os.path.join(SD, n)):
            shutil.copy2(os.path.join(SD, n), os.path.join(sd, n))
            caches.append(n)
    # (2026-10-29, the user: no four-core mode, the fourth core is the system's: no forwarder NPDM in the package.)
    nro_md5 = md5(nro)
    os.makedirs(os.path.join(out, 'elf'), exist_ok=True)
    shutil.copy2(elf, os.path.join(out, 'elf', f'renut_{nro_md5[:12]}.elf'))
    if os.path.isfile(cmap):
        shutil.copy2(cmap, os.path.join(out, 'elf', 'renut.map'))
    src = os.path.join(out, 'source')
    os.makedirs(src, exist_ok=True)
    subprocess.run(['git', '-C', ROOT, 'archive', '--format=zip', '-o',
                    os.path.join(src, f'nights-nx_src_{commit}.zip'), 'HEAD'], check=True)
    with zipfile.ZipFile(os.path.join(src, f'generated_{commit}.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
        gen = os.path.join(ROOT, 'app', 'generated')
        for base, _, files in os.walk(gen):
            for f in files:
                p = os.path.join(base, f)
                z.write(p, os.path.relpath(p, os.path.join(ROOT, 'app')))
    notes = open(os.path.join(ROOT, 'docs', 'package-notes.md'), encoding='utf-8').read()
    facts = (f'- Built from commit `{commit}`{" (with uncommitted source changes!)" if dirty else ""}, build folder '
             f'`{os.path.relpath(a.build, ROOT)}`; NRO md5 `{nro_md5}` ({os.path.getsize(nro) / 1e6:.1f} MB).\n'
             f'- Shader caches from the emulator included: {", ".join(caches) or "none"}.\n')
    open(os.path.join(out, 'PACKAGE_NOTES.md'), 'w', encoding='utf-8', newline='\n').write(
        notes.replace('<!-- facts -->\n', facts))
    total = sum(os.path.getsize(os.path.join(b, f)) for b, _, fs in os.walk(out) for f in fs)
    print(f'package {os.path.relpath(out, ROOT)}: {total / 1e6:.1f} MB, NRO md5 {nro_md5}, commit {commit}'
          f'{" (dirty)" if dirty else ""}')


if __name__ == '__main__':
    main()
