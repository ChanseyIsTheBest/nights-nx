#!/usr/bin/env python3
"""nights-nx (2026-10-28): pairs Peggle 2's functions (XDK 2.0.21256) with NiGHTS's (2.0.21173) by shape and call
sites, for moving Renut's Direct3D hooks N&B -> Peggle 2 (peggle2-nx out/renut_port/hook_map.json) -> NiGHTS.

The two libraries' link order differs, so a sequence alignment pairs little (tools/d3d_align_p2.py). Instead:
  seeds   functions whose shape (tools/d3d_match.py's token: opcodes and registers, no immediates) is unique in both
          images and equal; and the render / sampler state tables ({getter, setter, default} rows), whose default
          columns are equal word for word (NiGHTS 0x82600380 / 0x82600840);
  calls   for a paired function whose token sequences align (difflib opcodes over the tokens), each aligned `bl` pairs
          its two targets; repeated to a fixpoint. A target paired two different ways is a conflict and dropped.

  python -I tools/d3d_propagate_p2.py [--peggle D:/peggle 2/peggle2-nx]
Writes out/renut_map/p2_to_nights.json ({peggle: {nights, how}}) and prints the hook coverage.
"""
import argparse
import difflib
import glob
import hashlib
import json
import os
import re
import struct
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x82000000
MAX_INSNS = 3000
P2_TABLES = ((0x828DFDB8, 0x82600380, 101), (0x828E0278, 0x82600840, 20))


def starts_of(gen):
    s = set()
    for path in glob.glob(os.path.join(gen, '*_recomp.*.cpp')):
        for m in re.finditer(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', open(path, errors='replace').read(), re.M):
            s.add(int(m.group(1), 16))
    return sorted(s)


def token(x):
    op = x >> 26
    rd, ra, rb = (x >> 21) & 31, (x >> 16) & 31, (x >> 11) & 31
    if op in (16, 18):
        return (op, x & 1, rd if op == 16 else 0, ra if op == 16 else 0)
    if op in (19, 31, 63):
        return (op, (x >> 1) & 0x3FF, rd, ra, rb)
    if op == 59:
        return (op, (x >> 1) & 0x1F, rd, ra, rb)
    if op == 30:
        return (op, (x >> 2) & 7, rd, ra)
    if op == 4:
        return (op, x & 0x7FF, rd)
    return (op, rd, ra)


class Img:
    def __init__(self, path, starts, lo, hi):
        self.b = open(path, 'rb').read()
        self.starts = [s for s in starts if lo <= s < hi]
        self.f = {}
        for i, s in enumerate(self.starts):
            e = self.starts[i + 1] if i + 1 < len(self.starts) else s + 256
            n = min((e - s) // 4, MAX_INSNS)
            words = [struct.unpack_from('>I', self.b, s - BASE + 4 * k)[0] for k in range(n)]
            self.f[s] = (words, [token(w) for w in words])
        self.hash = {s: hashlib.sha1(repr(t).encode()).hexdigest() for s, (w, t) in self.f.items()}

    def calls(self, s):
        """[(index, target)] of the bl instructions of function s."""
        out = []
        for k, w in enumerate(self.f[s][0]):
            if w >> 26 == 18 and w & 1:
                d = w & 0x03FFFFFC
                if d & 0x02000000:
                    d -= 0x04000000
                out.append((k, (s + 4 * k + d) & 0xFFFFFFFF))
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--peggle', default='D:/peggle 2/peggle2-nx')
    # --ref-image / --ref-gen / --out-name: another reference game (nights-nx: banjo-nx's N&B for the hooks Peggle 2
    # does not link); the state tables are then not used (--no-tables).
    ap.add_argument('--ref-image', default='')
    ap.add_argument('--ref-gen', default='')
    ap.add_argument('--out-name', default='p2_to_nights.json')
    ap.add_argument('--no-tables', action='store_true')
    a = ap.parse_args()
    P = Img(a.ref_image or os.path.join(a.peggle, 'out', 'default_image.bin'),
            starts_of(a.ref_gen or os.path.join(a.peggle, 'app', 'generated', 'default')), 0x82000000, 0x83000000)
    N = Img(os.path.join(ROOT, 'out', 'renut_image.bin'), starts_of(os.path.join(ROOT, 'app', 'generated')),
            0x82130000, 0x825F0748)
    pairs, how = {}, {}
    ph, nh = Counter(P.hash.values()), Counter(N.hash.values())
    nby = {h: s for s, h in N.hash.items()}
    for s, h in P.hash.items():
        if ph[h] == 1 and nh.get(h) == 1 and len(P.f[s][0]) >= 6:
            pairs[s] = nby[h]
            how[s] = 'unique shape'
    for pa, na, rows in (() if a.no_tables else P2_TABLES):
        for i in range(rows):
            pr = struct.unpack_from('>3I', P.b, pa - BASE + 12 * i)
            nr = struct.unpack_from('>3I', N.b, na - BASE + 12 * i)
            assert pr[2] == nr[2], (hex(pa), i)
            for c in range(2):
                if pr[c] and nr[c]:
                    pairs[pr[c]] = nr[c]
                    how[pr[c]] = f'state table row {i} col {c}'
    conflicts = set()
    done = set()
    changed = True
    while changed:
        changed = False
        for p in list(pairs):
            n = pairs[p]
            if p in done or p not in P.f or n not in N.f:
                continue
            done.add(p)
            pt, nt = P.f[p][1], N.f[n][1]
            pc, nc = dict(P.calls(p)), dict(N.calls(n))
            if pt == nt:
                idx = [(k, k) for k in pc]
            else:
                sm = difflib.SequenceMatcher(None, pt, nt, autojunk=False)
                if sm.ratio() < 0.6:
                    continue
                idx = []
                for tag, i1, i2, j1, j2 in sm.get_opcodes():
                    if tag == 'equal' or (tag == 'replace' and i2 - i1 == j2 - j1):
                        idx += [(i1 + k, j1 + k) for k in range(i2 - i1) if i1 + k in pc]
            for i, j in idx:
                if j not in nc:
                    continue
                tp, tn = pc[i], nc[j]
                if tp in conflicts:
                    continue
                if tp in pairs:
                    if pairs[tp] != tn:
                        conflicts.add(tp)
                    continue
                pairs[tp] = tn
                how[tp] = f'call at +{4 * i:#x} of {p:08X}'
                changed = True
    for c in conflicts:
        if how.get(c, '').startswith('call'):
            pairs.pop(c, None)
    out = os.path.join(ROOT, 'out', 'renut_map')
    os.makedirs(out, exist_ok=True)
    json.dump({f'{k:08X}': {'nights': f'{v:08X}', 'how': how[k]} for k, v in sorted(pairs.items())},
              open(os.path.join(out, a.out_name), 'w'), indent=1)
    print('pairs', len(pairs), 'conflicts', len(conflicts), Counter(h.split(' ')[0] for h in how.values()))
    if a.ref_image:
        return
    hm = json.load(open(os.path.join(a.peggle, 'out', 'renut_port', 'hook_map.json')))
    have = miss = absent = 0
    missing = []
    for e in hm:
        pa = e.get('peggle_addr')
        if not pa:
            absent += 1
            continue
        pv = int(pa[4:], 16)
        if pv in pairs:
            have += 1
        else:
            miss += 1
            missing.append((pa, e['name']))
    print(f'hooks: {have} paired, {miss} not paired, {absent} absent in Peggle 2')
    for pa, nm in missing:
        print('  missing', pa, nm)


if __name__ == '__main__':
    main()
