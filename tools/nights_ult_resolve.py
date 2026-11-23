#!/usr/bin/env python3
"""Resolve the diff3 blocks tools/nights_ult_apply.py left in a file, block by block (nights-nx's rebase, 2026-11-22).

    python tools/nights_ult_resolve.py FILE CHOICE ...

One CHOICE per block, in file order: n (NiGHTS's side), b (the base's side), nb / bn (both, in that order), k (keep the
block as it is, to edit by hand). Line ends are kept.
"""
import sys


def main():
    path, choices = sys.argv[1], sys.argv[2:]
    data = open(path, 'rb').read()
    crlf = b'\r\n' in data
    lines = data.decode('utf-8').replace('\r\n', '\n').split('\n')
    out, i, block = [], 0, 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith('<<<<<<< '):
            out.append(line)
            i += 1
            continue
        ours, anc, theirs, part = [], [], [], 'ours'
        raw = [line]
        i += 1
        while not lines[i].startswith('>>>>>>> '):
            raw.append(lines[i])
            if lines[i].startswith('||||||| '):
                part = 'anc'
            elif lines[i] == '=======':
                part = 'theirs'
            else:
                {'ours': ours, 'anc': anc, 'theirs': theirs}[part].append(lines[i])
            i += 1
        raw.append(lines[i])
        i += 1
        if block >= len(choices):
            sys.exit('%s: block %d has no choice' % (path, block + 1))
        c = choices[block]
        block += 1
        if c == 'n':
            out.extend(ours)
        elif c == 'b':
            out.extend(theirs)
        elif c == 'nb':
            out.extend(ours + theirs)
        elif c == 'bn':
            out.extend(theirs + ours)
        elif c == 'k':
            out.extend(raw)
        else:
            sys.exit('unknown choice %r' % c)
    if block != len(choices):
        sys.exit('%s: %d blocks, %d choices' % (path, block, len(choices)))
    text = '\n'.join(out)
    if crlf:
        text = text.replace('\n', '\r\n')
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d blocks resolved' % (path, block))


if __name__ == '__main__':
    main()
