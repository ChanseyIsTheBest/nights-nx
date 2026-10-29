#!/usr/bin/env python3
"""nights-nx (2026-10-28): writes app/src/nights_dropped_cvars.cpp, the settings of banjo-nx's N&B-only sources
(app/CMakeLists.txt NIGHTS_NB_SOURCES, not built for NiGHTS) that the shared code still declares: each definition is
copied as it is (type, default, category, help, range), so a toml that sets one still parses and the enhancement
framework still names it. Run again when the list or the shared code's declarations change.

  python -I tools/make_dropped_cvars.py
"""
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'app')
OUT = os.path.join(APP, 'src', 'nights_dropped_cvars.cpp')


def dropped_sources():
    text = open(os.path.join(APP, 'CMakeLists.txt'), encoding='utf-8').read()
    block = text[text.index('set(NIGHTS_NB_SOURCES'):]
    block = block[:block.index(')')]
    return [os.path.join(APP, l.strip()) for l in block.splitlines()[1:] if l.strip().startswith('src/')]


def statements(text):
    """Each REXCVAR_DEFINE_* statement, from its macro to the ';' that ends it (outside strings)."""
    out = []
    for m in re.finditer(r'REXCVAR_DEFINE_(\w+)\(\s*(\w+)', text):
        i, depth, in_str = m.start(), 0, False
        j = i
        while j < len(text):
            c = text[j]
            if in_str:
                if c == '\\':
                    j += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            elif c == ';' and depth == 0:
                break
            j += 1
        out.append((m.group(2), text[i:j + 1]))
    return out


def main():
    dropped = [p for p in dropped_sources() if os.path.isfile(p)]
    # The headers of banjo-nx's ring renderer (src/me, not built) define settings too (banjo_d3d_native.h, ...).
    dropped += glob.glob(os.path.join(APP, 'src', 'me', '**', '*.h'), recursive=True)
    dropped_set = {os.path.normcase(os.path.abspath(p)) for p in dropped}
    defs = {}
    for p in dropped:
        for name, stmt in statements(open(p, encoding='utf-8', errors='replace').read()):
            defs.setdefault(name, (stmt, os.path.relpath(p, APP)))
    kept_defined, declared = set(), set()
    for p in glob.glob(os.path.join(APP, 'src', '**', '*.*'), recursive=True):
        if not p.endswith(('.cpp', '.h', '.inc')) or os.path.normcase(os.path.abspath(p)) in dropped_set:
            continue
        if os.path.abspath(p) == os.path.abspath(OUT):
            continue
        t = open(p, encoding='utf-8', errors='replace').read()
        declared |= set(re.findall(r'REXCVAR_DECLARE\(\s*[\w:<>]+\s*,\s*(\w+)\s*\)', t))
        kept_defined |= {n for n, _ in statements(t)}
    need = sorted(n for n in declared if n in defs and n not in kept_defined)
    lines = ['// nights-nx (tools/make_dropped_cvars.py, generated): the settings of banjo-nx\'s N&B-only sources',
             '// (app/CMakeLists.txt NIGHTS_NB_SOURCES, not built for NiGHTS) that the shared code still declares,',
             '// defined here as they were there. Their features are not in this build; the values only keep tomls',
             '// and the enhancement framework\'s report working.', '',
             '#include <cstdint>', '#include <string>', '', '#include <rex/cvar.h>', '']
    for n in need:
        stmt, src = defs[n]
        lines.append(f'// {src}')
        lines.append(stmt)
        lines.append('')
    open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print(f'make_dropped_cvars: {len(need)} settings from {len(dropped)} dropped files -> {os.path.relpath(OUT, ROOT)}')


if __name__ == '__main__':
    main()
