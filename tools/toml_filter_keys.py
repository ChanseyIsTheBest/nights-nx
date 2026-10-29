#!/usr/bin/env python3
"""nights-nx (2026-10-29): keeps the lines of a toml (banjo-nx's shipped renut.toml) whose keys this build defines, and
drops the keys of banjo-nx's N&B-only sources (app/CMakeLists.txt NIGHTS_NB_SOURCES, the stubs of
app/src/nights_dropped_cvars.cpp) and of its ring renderer (app/src/me). Comments are kept.

  python -I tools/toml_filter_keys.py <in.toml> <out.toml>
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build_cvars():
    cm = open(os.path.join(ROOT, 'app', 'CMakeLists.txt'), encoding='utf-8').read()
    blk = cm[cm.index('set(NIGHTS_NB_SOURCES'):]
    blk = blk[:blk.index(')')]
    dropped = {os.path.normcase(os.path.abspath(os.path.join(ROOT, 'app', l.strip())))
               for l in blk.splitlines()[1:] if l.strip().startswith('src/')}
    names = set()
    for p in glob.glob(os.path.join(ROOT, 'app', 'src', '**', '*.*'), recursive=True) + \
            glob.glob(os.path.join(ROOT, 'sdk', 'src', '**', '*.*'), recursive=True):
        q = os.path.normcase(os.path.abspath(p))
        if not p.endswith(('.cpp', '.h')) or q in dropped or os.sep + 'me' + os.sep in q:
            continue
        if os.path.basename(p) == 'nights_dropped_cvars.cpp':
            continue
        for m in re.finditer(r'REXCVAR_DEFINE_\w+\(\s*(\w+)', open(p, encoding='utf-8', errors='replace').read()):
            names.add(m.group(1))
    return names


def main():
    names = build_cvars()
    keep, dropped = [], []
    for line in open(sys.argv[1], encoding='utf-8').read().split('\n'):
        m = re.match(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=', line)
        if m and m.group(1) not in names:
            dropped.append(m.group(1))
            continue
        keep.append(line)
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write('\n'.join(keep))
    print(f'toml_filter_keys: {len(names)} settings in the build; {len(dropped)} keys dropped: {" ".join(dropped)}')


if __name__ == '__main__':
    main()
