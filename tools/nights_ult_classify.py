#!/usr/bin/env python3
"""Classify every file for nights-nx's rebase on the ultimate base (2026-11-22).

nights-nx's own form of banjo-nx-ult's tools/ult_rebase_classify.py. Revisions (all in nights-nx's object store; the
base fetched into refs/ultbase/ultimate):

  E  nights-nx's first commit (e1cdfa43): banjo-nx's working tree of 2026-10-28 (1889dab6 + 41 modified files)
  N  nights-nx's master (1.0.2 and the packed files / Opus / H.265 work of 2026-11-22)
  S  the base's first commit (83b4ae77): banjo-nx 894d3256 (2026-10-29, after its title update 3 work) + 13 files
  M  the base's 838c8c41: Renut's hooks and device layout moved to Katamari's XDK 2.0.5632
  T  the base's tip

Contents are compared with CR removed. Writes out/ult_rebase_classes.tsv: path, class, which revisions hold it, and
whether the file is one of the per-XDK files (tools/ult_port_sync.py's DEFAULT_HAND).

  same        T == N
  base        N == E (NiGHTS did not change it since its fork): take T
  nights      T == E (the base holds NiGHTS's fork's text): take N
  merge       both changed since E
  base-new    only in T (not in E)
  nights-new  only in N (not in E)
  base-del    in E and N (N == E), not in T: the base deleted it
  nights-del  in E and T (T == E), not in N: NiGHTS deleted it
  conflict-del / del-both  the rest
"""
import os
import subprocess
import sys

REVS = {'E': 'e1cdfa43', 'N': 'master', 'S': '83b4ae77', 'M': '838c8c41', 'T': 'refs/ultbase/ultimate'}
HAND = ('app/src/renut_engine/gpu/d3d/hooks/', 'app/src/renut_engine/gpu/d3d/d3d.h',
        'app/src/renut_engine/gpu/d3d/d3d_native.h', 'app/src/renut_engine/gpu/d3d/d3d_native_state.cpp',
        'app/src/renut_engine/gpu/banjo_shared_hooks.h')


def tree(rev):
    out = subprocess.run(['git', 'ls-tree', '-r', rev], capture_output=True, check=True).stdout
    files = {}
    for line in out.split(b'\n'):
        if not line:
            continue
        meta, path = line.split(b'\t', 1)
        mode, kind, sha = meta.split()
        if kind != b'blob':
            continue
        files[path.decode('utf-8', 'surrogateescape')] = sha.decode()
    return files


class Blobs:
    def __init__(self):
        self.p = subprocess.Popen(['git', 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.cache = {}

    def get(self, sha):
        if sha in self.cache:
            return self.cache[sha]
        self.p.stdin.write(sha.encode() + b'\n')
        self.p.stdin.flush()
        header = self.p.stdout.readline().split()
        size = int(header[2])
        data = self.p.stdout.read(size)
        self.p.stdout.read(1)
        norm = data.replace(b'\r\n', b'\n')
        self.cache[sha] = norm
        return norm


def classify(e, n, t):
    if t is not None and n is not None and t == n:
        return 'same'
    if t is not None and n is not None:
        if n == e:
            return 'base'
        if t == e:
            return 'nights'
        return 'merge'
    if t is not None:  # not in N
        if e is None:
            return 'base-new'
        if t == e:
            return 'nights-del'
        return 'conflict-del'
    if n is not None:  # not in T
        if e is None:
            return 'nights-new'
        if n == e:
            return 'base-del'
        return 'conflict-del'
    return 'del-both'


def main():
    trees = {k: tree(v) for k, v in REVS.items()}
    blobs = Blobs()
    paths = sorted(set(trees['E']) | set(trees['N']) | set(trees['T']))

    def content(k, p):
        sha = trees[k].get(p)
        return None if sha is None else blobs.get(sha)

    rows = []
    for p in paths:
        e, n, t = content('E', p), content('N', p), content('T', p)
        have = ''.join(x for x in 'ENSMT' if p in trees[x])
        cls = classify(e, n, t)
        rows.append((p, cls, have, 'hand' if p.startswith(HAND) else ''))
    os.makedirs('out', exist_ok=True)
    with open('out/ult_rebase_classes.tsv', 'w', encoding='utf-8') as f:
        f.write('path\tclass\tin\tper_xdk\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    counts = {}
    for _, cls, _, _ in rows:
        counts[cls] = counts.get(cls, 0) + 1
    for cls, n in sorted(counts.items()):
        print('%-13s %d' % (cls, n))


if __name__ == '__main__':
    sys.exit(main())
