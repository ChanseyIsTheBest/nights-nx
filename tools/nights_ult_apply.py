#!/usr/bin/env python3
"""Apply out/ult_rebase_classes.tsv (tools/nights_ult_classify.py) to this worktree: nights-nx's rebase (2026-11-22).

nights-nx's form of banjo-nx-ult's tools/ult_rebase_apply.py. The worktree starts as the base's tip (T). For each file:

  nights, nights-new  N's text is written (a file NiGHTS added or changed where the base kept the fork's text)
  merge               a three-way merge into the file: ours N, theirs T, ancestor E (nights-nx's fork), or for the
                      per-XDK files M (the base's 838c8c41, the Katamari move: its layout is the one T's later ports
                      wrote against, so only their changes come in and Katamari's addresses stay out); conflicts are
                      left as diff3 blocks and listed
  everything else     untouched (T's text, or deleted where the base deleted it)

Only paths given on the command line (prefixes) are touched, so each piece can be applied, reviewed and committed on
its own:
    python tools/nights_ult_apply.py [--dry-run] PREFIX ...
Line ends: the result is written with T's line ends (CRLF if T's file has them), N's for a file only N has.
"""
import argparse
import os
import subprocess
import sys
import tempfile

E, N, M, T = 'e1cdfa43', 'master', '838c8c41', 'refs/ultbase/ultimate'


def show(rev, path):
    r = subprocess.run(['git', 'show', '%s:%s' % (rev, path)], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefixes', nargs='+')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rows = [l.rstrip('\n').split('\t') for l in open('out/ult_rebase_classes.tsv', encoding='utf-8')][1:]
    done = {'written': 0, 'clean': 0, 'conflict': 0}
    for path, cls, have, per_xdk in rows:
        if not any(path == p or path.startswith(p.rstrip('/') + '/') or path.startswith(p) for p in a.prefixes):
            continue
        if cls in ('nights', 'nights-new'):
            data = show(N, path)
            print('take N    %s' % path)
            done['written'] += 1
            if not a.dry_run:
                os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
                open(path, 'wb').write(data)
        elif cls == 'merge':
            anc_rev = M if per_xdk == 'hand' else E
            ours, anc, theirs = show(N, path), show(anc_rev, path), show(T, path)
            if anc is None:
                anc = b''
            crlf = theirs is not None and b'\r\n' in theirs
            norm = [x.replace(b'\r\n', b'\n') for x in (ours, anc, theirs)]
            with tempfile.TemporaryDirectory() as tmp:
                names = []
                for tag, data in zip(('ours', 'base', 'theirs'), norm):
                    p = os.path.join(tmp, tag)
                    open(p, 'wb').write(data)
                    names.append(p)
                r = subprocess.run(['git', 'merge-file', '-p', '--diff3', '-L', 'nights', '-L',
                                    'ancestor ' + anc_rev, '-L', 'base', *names], capture_output=True)
            out = r.stdout
            if crlf:
                out = out.replace(b'\n', b'\r\n')
            state = 'clean' if r.returncode == 0 else 'conflict'
            done[state] += 1
            print('%-9s %s%s' % (state, path, '' if r.returncode <= 0 else '  (%d blocks)' % r.returncode))
            if not a.dry_run:
                open(path, 'wb').write(out)
    print(done)


if __name__ == '__main__':
    sys.exit(main())
