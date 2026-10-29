#!/usr/bin/env python3
"""banjo-nx (2026-10-22): which recompiled game functions to localize in bulk, from console profiles.

The method is bejeweled3-nx's (D:/peggle 2/bejeweled3-nx/docs/optimising-functions.md, "Bulk localization"): every
function that still runs as generated code and has at least --min-samples console-profile samples (all threads)
becomes a localized copy (tools/localize_native.py --bulk), except
  - tiny leaves: at most --tiny guest instructions and no call (the generator already inlines them into their callers,
    and a hook made one slower in bejeweled3-nx);
  - functions the copy tool refuses (atomics, the global lock, setjmp, host calls inside: localize_native.fits());
  - anything an app or SDK source mentions (their own hooks: localize_native.Hooks);
  - the functions of localized.txt and hand.txt (already natives).

Input: one or more "PROFILE ELF" pairs: logs/rex/rex_profile.log of a console run (SDK stack sampling, "pc
image+0x... xN" per thread every 10 s) and the ELF of the NRO that wrote it (builds/<package>/renut_<md5>.elf). A
generated function shows in the ELF as sub_XXXXXXXX (and __imp__sub_XXXXXXXX at the same address); a hooked one as
two addresses (the hook, and __imp__ the original).

  python tools/switch_hot_bulk.py PROFILE ELF [PROFILE ELF ...] [--from-report N] [--top 40]
  python tools/switch_hot_bulk.py PROFILE ELF ... --write app/src/cpu/hot/localized_bulk.txt [--min-samples 3]
"""
import argparse
import bisect
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WIN = re.compile(r'^==== ([0-9.]+) s \| mode: (\w+) \| game ([0-9.]+) fps')
THREAD = re.compile(r'^-- thread (\d+) "([^"]*)": CPU ([0-9.]+)%')
PC = re.compile(r'^\s+[0-9.]+%\s+pc image\+0x([0-9a-f]+) x(\d+)')
NM = next((p for p in (os.path.join(os.environ.get('DEVKITPRO', ''), 'devkitA64', 'bin', 'aarch64-none-elf-nm.exe'),
                       'C:/devkitPro/devkitA64/bin/aarch64-none-elf-nm.exe') if os.path.isfile(p)), 'aarch64-none-elf-nm')


def symbols(elf):
    out = subprocess.run([NM, '-n', '--defined-only', elf], check=True, capture_output=True, text=True,
                         errors='replace').stdout
    starts, names = [], []
    by_name = {}
    for line in out.splitlines():
        p = line.split(' ', 2)
        if len(p) != 3 or p[1] not in 'TtWw':
            continue
        a = int(p[0], 16)
        by_name.setdefault(p[2], a)
        if starts and starts[-1] == a:
            # Several names at one address: a generated function's sub_X and __imp__sub_X. Keep the plain one.
            if names[-1].startswith('__imp__sub_') and p[2].startswith('sub_'):
                names[-1] = p[2]
            continue
        starts.append(a)
        names.append(p[2])
    return starts, names, by_name


def demangled_kind(name, by_name):
    """('gen', 'sub_X') for generated code, ('hook', 'sub_X') for a hook's copy or original, ('host', name)."""
    m = re.fullmatch(r'(?:__imp__)?(sub_[0-9A-F]{8})', name)
    if m:
        sub = m.group(1)
        if sub in by_name and '__imp__' + sub in by_name and by_name[sub] != by_name['__imp__' + sub]:
            return 'hooked', sub  # the function has a hook: these samples are in its original (cold) or the hook
        return 'gen', sub
    m = re.search(r'banjo3hot\d+n_([0-9A-F]{8})', name)  # _ZN5banjo3hot10n_823A9E80...
    if m:
        return 'copy', 'sub_' + m.group(1)
    return 'host', name


def parse(profile, elf, from_report):
    starts, names, by_name = symbols(elf)
    per_func = collections.Counter()           # sub_X -> generated-code samples, all threads
    per_thread = collections.defaultdict(collections.Counter)  # thread name -> {(kind, name): samples}
    totals = collections.Counter()             # thread name -> samples
    report = 0
    thread = None
    for line in open(profile, encoding='utf-8', errors='replace'):
        if WIN.match(line):
            report += 1
            thread = None
            continue
        m = THREAD.match(line)
        if m:
            thread = re.sub(r'[0-9A-F]{8}$', '', m.group(2)) or '?'
            continue
        m = PC.match(line)
        if not m or thread is None or report <= from_report:
            continue
        off, n = int(m.group(1), 16), int(m.group(2))
        k = bisect.bisect_right(starts, off) - 1
        name = names[k] if k >= 0 else '?'
        kind, what = demangled_kind(name, by_name)
        totals[thread] += n
        per_thread[thread][(kind, what)] += n
        if kind == 'gen':
            per_func[what] += n
    return per_func, per_thread, totals


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pairs', nargs='+', help='PROFILE ELF [PROFILE ELF ...]')
    ap.add_argument('--from-report', type=int, default=0, help='skip the first N 10-s reports of each profile')
    ap.add_argument('--top', type=int, default=30)
    ap.add_argument('--threads', type=int, default=6, help='threads shown (the busiest)')
    ap.add_argument('--write', help='write the bulk list here (localize_native.py --bulk)')
    ap.add_argument('--min-samples', type=float, default=3)
    ap.add_argument('--tiny', type=int, default=12, help='leaves of at most this many guest instructions stay out')
    a = ap.parse_args()
    if len(a.pairs) % 2:
        ap.error('PROFILE ELF pairs')
    per_func = collections.Counter()
    per_thread = collections.defaultdict(collections.Counter)
    totals = collections.Counter()
    for k in range(0, len(a.pairs), 2):
        f, t, s = parse(a.pairs[k], a.pairs[k + 1], a.from_report)
        per_func.update(f)
        for th, c in t.items():
            per_thread[th].update(c)
        totals.update(s)
    for th, n in totals.most_common(a.threads):
        kinds = collections.Counter()
        for (kind, _), c in per_thread[th].items():
            kinds[kind] += c
        print(f'== {th}: {n} samples: ' + ', '.join(f'{k} {100 * c / n:.1f}%' for k, c in kinds.most_common()))
        for (kind, what), c in per_thread[th].most_common(a.top):
            print(f'   {100 * c / n:5.2f}%  {c:6d}  {kind:6s} {what[:110]}')
    if not a.write:
        return

    import localize_native as ln
    index = ln.Index(ln.GEN)
    listed = [e[0] for e in ln.list_entries(ln.LIST)]
    hand = ln.hand_entries()
    hooks = ln.Hooks(listed)
    sizes = ln.addresses(ln.GEN)
    # The text this tool reads is the final tree; localize_native.py makes its copies before the late steps of
    # tools/post_codegen.py. Their forms are taken back here as they are there: store cards (REX_CARD(ea), in front of
    # raw vector stores and dcbz memsets) come after the copies (store_cards.py also marks the copies' stores).
    import store_cards

    def raw_ok(index, sub):
        for _, l in index.body(sub):
            l = store_cards.restore_text(l)
            if 'REX_RAW_ADDR(' in l and not (ln.RAW_VECTOR_STORE.match(l) or ln.RAW_MEMSET.match(l) or
                                             ln.RAW_LOAD.match(l)):
                return False
        return True

    # The script interpreters' jump tables are threaded by switch_goto.py (a later step): a copy would undo that.
    post = open(os.path.join(ROOT, 'tools', 'post_codegen.py'), encoding='utf-8').read()
    threaded = set(re.findall(r'sub_[0-9A-F]{8}', post[post.find("tool('switch_goto.py'), '--gen', gen, '--thread'"):][:300]))
    rows, left = [], collections.Counter()
    for sub, n in per_func.most_common():
        if n < a.min_samples:
            break
        f = index.funcs.get(sub)
        why = None
        if f is None:
            why = 'not a generated function'
        elif sub in listed or sub in hand:
            why = 'already a native'
        elif not f['callees'] and not f['unknown'] and sizes.get(sub, (0, 4 * (a.tiny + 1)))[1] // 4 <= a.tiny:
            why = 'tiny leaf'
        elif hooks.replaced(sub):
            why = 'an app or SDK source mentions it'
        elif sub in threaded:
            why = 'threaded dispatch (switch_goto.py)'
        else:
            why = ln.fits(index, hooks, sub)
            if why == 'raw guest access' and raw_ok(index, sub):
                why = None
        if why:
            left[why.split(':')[0][:40]] += 1
            continue
        rows.append((sub, n, sizes.get(sub, (0, 0))[1] // 4, len(f['callees'])))
    lines = ['# banjo-nx: the bulk list of localized copies (tools/localize_native.py --bulk; their hooks go into the',
             '# shard files app/generated/hot/banjo_hot_bulk_<k>.inc). GENERATED by tools/switch_hot_bulk.py from the console',
             '# profiles ' + ', '.join(os.path.relpath(a.pairs[k], ROOT).replace('\\', '/') for k in range(0, len(a.pairs), 2)),
             f'# (every function still in generated code with at least {a.min_samples:g} samples, all threads; tiny leaves of',
             f'# {a.tiny} instructions or fewer, refused forms and app-mentioned addresses left out). Edit by hand freely: a',
             '# line is "<address> <what>".', '']
    for sub, n, ins, calls in rows:
        lines.append(f'{sub[4:]} {n} console samples, {ins} instructions, {calls} direct callees')
    open(a.write, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{len(rows)} functions written to {a.write}; left out: ' + ', '.join(f'{k} {v}' for k, v in left.most_common()))


if __name__ == '__main__':
    main()
