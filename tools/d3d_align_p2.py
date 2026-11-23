#!/usr/bin/env python3
"""nights-nx (2026-10-28): pairs Peggle 2's Direct3D library functions (XDK 2.0.21256) with NiGHTS's (2.0.21173).

Both games link the same Direct3D library, a few months apart, so its functions come in the same order with the same
shape. peggle2-nx's out/renut_port/hook_map.json pairs every hook of Renut's renderer (banjo-nx: N&B's addresses) with
Peggle 2's function; this pairs Peggle 2's library with NiGHTS's, so the hooks can move N&B -> Peggle 2 -> NiGHTS.

  shape  = each instruction's opcode, extended opcode and register fields (immediates and branch displacements left
           out: tools/d3d_match.py's token), hashed per function;
  anchor = difflib's matching blocks of the two libraries' function-hash sequences (equal runs in the same order);
  fill   = between two anchors, runs of the same length paired in order, each with its shape ratio.

Also pairs the render-state and sampler-state tables ({getter, setter, default} rows of 12 bytes): NiGHTS's are found
as the only places whose default column equals Peggle 2's word for word.

  python -I tools/d3d_align_p2.py [--peggle D:/peggle 2/peggle2-nx] [--out out/renut_map]
Writes out/renut_map/p2_to_nights.json and anchor.txt.
"""
import argparse
import bisect
import difflib
import glob
import hashlib
import json
import os
import re
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x82000000
MAX_INSNS = 2000
# Peggle 2's state tables (peggle2-nx docs/peggle2-notes.md: render states 101 rows, sampler states 20).
P2_TABLES = ((0x828DFDB8, 101), (0x828E0278, 20))


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


def funcs(image, starts, lo, hi):
    out = []
    for i, s in enumerate(starts):
        if not lo <= s < hi:
            continue
        e = starts[i + 1] if i + 1 < len(starts) else s + 4 * 64
        n = min((e - s) // 4, MAX_INSNS)
        toks = [token(struct.unpack_from('>I', image, s - BASE + 4 * k)[0]) for k in range(n)]
        out.append((s, toks, hashlib.sha1(repr(toks).encode()).hexdigest()[:16]))
    return out


def find_tables(p2img, nimg):
    pairs = []
    for addr, rows in P2_TABLES:
        defaults = [struct.unpack_from('>I', p2img, addr - BASE + 12 * i + 8)[0] for i in range(rows)]
        pat = b''.join(struct.pack('>I', d) for d in defaults)
        # the defaults column has a stride: search the first row's 3 words' default at stride 12
        hits = []
        first = struct.pack('>I', defaults[0])
        off = nimg.find(first)
        while off != -1:
            cand = off - 8
            if cand >= 0 and cand % 4 == 0 and cand + 12 * rows <= len(nimg) and all(
                    struct.unpack_from('>I', nimg, cand + 12 * i + 8)[0] == defaults[i] for i in range(rows)):
                hits.append(BASE + cand)
            off = nimg.find(first, off + 1)
        pairs.append((addr, rows, hits))
    return pairs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--peggle', default='D:/peggle 2/peggle2-nx')
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'renut_map'))
    ap.add_argument('--p2-range', default='82610000:82638000')
    ap.add_argument('--n-range', default='')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    p2img = open(os.path.join(a.peggle, 'out', 'default_image.bin'), 'rb').read()
    nimg = open(os.path.join(ROOT, 'out', 'renut_image.bin'), 'rb').read()
    p2s = starts_of(os.path.join(a.peggle, 'app', 'generated', 'default'))
    ns = starts_of(os.path.join(ROOT, 'app', 'generated'))
    lo, hi = (int(x, 16) for x in a.p2_range.split(':'))
    P = funcs(p2img, p2s, lo, hi)
    # Locate NiGHTS's library: where Peggle's distinctive (long, unique) functions match exactly.
    allN = funcs(nimg, ns, 0x82130000, 0x825F0748)
    byhash = {}
    for s, t, h in allN:
        byhash.setdefault(h, []).append(s)
    hits = sorted(byhash[h][0] for s, t, h in P if len(t) >= 24 and len(byhash.get(h, [])) == 1)
    if not hits:
        raise SystemExit('no exact match of a long Peggle 2 function in NiGHTS')
    med = hits[len(hits) // 2]
    nlo, nhi = hits[max(0, len(hits) // 10)] - 0x8000, hits[min(len(hits) - 1, len(hits) * 9 // 10)] + 0x8000
    nlo, nhi = min(nlo, hits[0] - 0x1000), max(nhi, hits[-1] + 0x1000)
    if a.n_range:
        nlo, nhi = (int(x, 16) for x in a.n_range.split(':'))
    N = [f for f in allN if nlo <= f[0] < nhi]
    print(f'Peggle 2 library {lo:08X}-{hi:08X}: {len(P)} functions; NiGHTS window {nlo:08X}-{nhi:08X}: {len(N)} '
          f'functions ({len(hits)} long exact matches, median {med:08X})')
    sm = difflib.SequenceMatcher(None, [h for _, _, h in P], [h for _, _, h in N], autojunk=False)
    pairs = {}
    lines = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                pairs[P[i1 + k][0]] = (N[j1 + k][0], 'exact', 1.0)
            lines.append(f'= {i2 - i1:4d}  {P[i1][0]:08X}..  {N[j1][0]:08X}..')
        elif tag == 'replace' and i2 - i1 == j2 - j1:
            for k in range(i2 - i1):
                r = difflib.SequenceMatcher(None, P[i1 + k][1], N[j1 + k][1], autojunk=False).ratio()
                pairs[P[i1 + k][0]] = (N[j1 + k][0], 'aligned', round(r, 3))
                lines.append(f'~ {P[i1 + k][0]:08X} {N[j1 + k][0]:08X} {r:.3f} ({len(P[i1+k][1])}/{len(N[j1+k][1])})')
        else:
            # unequal runs: best shape per Peggle function inside the NiGHTS run
            for k in range(i1, i2):
                best = None
                for m in range(j1, j2):
                    r = difflib.SequenceMatcher(None, P[k][1], N[m][1], autojunk=False).ratio()
                    if best is None or r > best[1]:
                        best = (N[m][0], r)
                if best and best[1] >= 0.8:
                    pairs[P[k][0]] = (best[0], 'run-best', round(best[1], 3))
                lines.append(f'{tag[0]} {P[k][0]:08X} ' + (f'{best[0]:08X} {best[1]:.3f}' if best else '-'))
    tables = find_tables(p2img, nimg)
    for addr, rows, found in tables:
        print(f'state table {addr:08X} ({rows} rows): NiGHTS {[hex(x) for x in found]}')
        if len(found) == 1:
            for i in range(rows):
                pr = struct.unpack_from('>3I', p2img, addr - BASE + 12 * i)
                nr = struct.unpack_from('>3I', nimg, found[0] - BASE + 12 * i)
                for c in range(2):
                    if pr[c] and nr[c]:
                        pairs[pr[c]] = (nr[c], f'state table {addr:08X} row {i} col {c}', 1.0)
    json.dump({f'{k:08X}': {'nights': f'{v[0]:08X}', 'how': v[1], 'ratio': v[2]} for k, v in sorted(pairs.items())},
              open(os.path.join(a.out, 'p2_to_nights.json'), 'w'), indent=1)
    open(os.path.join(a.out, 'anchor.txt'), 'w').write('\n'.join(lines) + '\n')
    from collections import Counter
    print('pairs:', len(pairs), Counter(v[1].split(' ')[0] for v in pairs.values()))


if __name__ == '__main__':
    main()
