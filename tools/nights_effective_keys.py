#!/usr/bin/env python3
"""The Switch build's effective settings, nights-nx 1.0.2 (master) against this tree (2026-11-22; sa2-nx-ult's
tools/sa2_effective_keys.py's idea).

For each tree: every REXCVAR_DEFINE_* in app/src, sdk/src and sdk/include with its default as written, then the
compiled-in settings (app/nights_switch_defaults.toml) and the player's (app/nights_switch.toml) applied over them.
Prints the keys both trees define whose effective value differs, the old keys with no definition here, and (with
--new) the keys only this tree defines with their defaults.

    python tools/nights_effective_keys.py [--old master] [--new]
"""
import argparse
import re
import subprocess
import sys

DEFINE = re.compile(r'REXCVAR_DEFINE_(BOOL|INT32|INT64|UINT32|UINT64|DOUBLE|FLOAT|STRING)\(\s*([a-z_0-9]+)\s*,\s*'
                    r'("(?:[^"\\]|\\.)*"|[^,]+?)\s*,', re.S)
ROOTS = ('app/src', 'sdk/src', 'sdk/include')


def git_files(rev):
    out = subprocess.run(['git', 'ls-tree', '-r', '--name-only', rev, '--', *ROOTS], capture_output=True, text=True,
                         check=True).stdout.split('\n')
    return [p for p in out if p.endswith(('.cpp', '.h', '.cc', '.c', '.inc'))]


def show(rev, path):
    r = subprocess.run(['git', 'show', '%s:%s' % (rev, path)], capture_output=True)
    return r.stdout.decode('utf-8', 'replace') if r.returncode == 0 else ''


def read_work(path):
    try:
        return open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return ''


def norm(kind, v):
    v = v.strip()
    if kind == 'STRING':
        return v.strip('"')
    if kind == 'BOOL':
        return 'true' if v in ('true', '1') else 'false' if v in ('false', '0') else v
    v = re.sub(r"(?<=\d)'(?=\d)", '', v).rstrip('uUlLfF')
    try:
        return str(float(v)) if kind in ('DOUBLE', 'FLOAT') else str(int(v, 0))
    except ValueError:
        return v


def defaults(reader, files):
    keys = {}
    for p in files:
        for m in DEFINE.finditer(reader(p)):
            keys.setdefault(m.group(2), (m.group(1), norm(m.group(1), m.group(3)), p))
    return keys


def toml_keys(text, kinds):
    out = {}
    for line in text.split('\n'):
        line = line.split('#')[0].strip()
        if line.startswith('['):
            break  # the [input_pN] tables
        m = re.match(r'^([a-z_0-9]+)\s*=\s*(.+)$', line)
        if m:
            k = m.group(1)
            kind = kinds.get(k, ('STRING',))[0]
            out[k] = norm(kind, m.group(2))
    return out


def effective(reader, files, toml_reader):
    d = defaults(reader, files)
    eff = {k: v[1] for k, v in d.items()}
    for name in ('app/nights_switch_defaults.toml', 'app/nights_switch.toml'):
        for k, v in toml_keys(toml_reader(name), d).items():
            eff[k] = v
    return d, eff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default='master')
    ap.add_argument('--new', action='store_true', help='also list the keys only this tree defines')
    a = ap.parse_args()
    old_files = git_files(a.old)
    new_files = subprocess.run(['git', 'ls-files', '--', *ROOTS], capture_output=True, text=True,
                               check=True).stdout.split('\n')
    new_files = [p for p in new_files if p.endswith(('.cpp', '.h', '.cc', '.c', '.inc'))]
    od, oe = effective(lambda p: show(a.old, p), old_files, lambda p: show(a.old, p))
    nd, ne = effective(read_work, new_files, read_work)
    both = sorted(set(od) & set(nd))
    diff = [k for k in both if oe[k] != ne[k]]
    print('%d keys in 1.0.2 (%s), %d here, %d in both; %d differ in effect:' % (len(od), a.old, len(nd), len(both),
                                                                               len(diff)))
    for k in diff:
        print('  %-48s 1.0.2 %-12s here %-12s (%s)' % (k, oe[k], ne[k], nd[k][2]))
    gone = sorted(set(od) - set(nd))
    print('%d keys of 1.0.2 not defined here:' % len(gone))
    for k in gone:
        print('  %-48s 1.0.2 %-12s (%s)' % (k, oe[k], od[k][2]))
    if a.new:
        new = sorted(set(nd) - set(od))
        print('%d keys only here:' % len(new))
        for k in new:
            print('  %-48s %-12s (%s)' % (k, ne[k], nd[k][2]))


if __name__ == '__main__':
    sys.exit(main())
