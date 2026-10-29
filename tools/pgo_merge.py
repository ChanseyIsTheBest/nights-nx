#!/usr/bin/env python3
"""Profile-guided optimisation, step 2 of 3 (banjo-nx, 2026-10-21; sa2-nx's tools/win/pgo_merge.ps1 in Python, since
PowerShell scripts do not run on this PC).

Step 1 is the instrumented NRO (a build folder configured with -DBANJO_PGO=generate), played on the console or in
Ryujinx: it writes pgo/banjo_pgo_<N>.stream next to the NRO (one per session, app/src/banjo_pgo.cpp). Copy that pgo
folder to the PC and run this: every session's counts are added into the .gcda files of the profile folder
(gcov-tool merge-stream), then their summary is filled in (tools/pgo_summary.py: the stream API writes it as zero).
Step 3: a build folder configured with -DBANJO_PGO=use (and -DBANJO_SWITCH_LTO=ON); docs/pgo-lto.md.

  python tools/pgo_merge.py <folder with the .stream files> [<profile folder>]

The profile folder is pgo/ at the root of the repository (the build's BANJO_PGO_DIR): one .gcda per object, named by
the object's path in the build folder with # for each separator (GCC's names with -fprofile-prefix-path). The streams
name the files they fill with the profile folder of the PC that built the instrumented NRO: merge on that PC.

merge-stream ADDS to .gcda files already there: a second run with the same streams would count them twice, so this
stops when the profile folder already has .gcda files (move them aside first).
"""
import os
import subprocess
import sys

TOOL = 'C:/devkitPro/devkitA64/bin/aarch64-none-elf-gcov-tool.exe'


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    streams = sys.argv[1]
    out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(root, 'pgo')
    if not os.path.exists(TOOL):
        sys.exit('gcov-tool not found: ' + TOOL)
    files = sorted(f for f in os.listdir(streams) if f.startswith('banjo_pgo_') and f.endswith('.stream'))
    if not files:
        sys.exit('no banjo_pgo_*.stream in ' + streams)
    os.makedirs(out, exist_ok=True)
    if any(f.endswith('.gcda') for f in os.listdir(out)):
        sys.exit(out + ' already has .gcda files: move them aside first (merge-stream would add to them)')
    for f in files:
        path = os.path.join(streams, f)
        print('merging %s (%.1f MB)' % (f, os.path.getsize(path) / 1048576.0), flush=True)
        r = subprocess.run([TOOL, 'merge-stream', path])
        if r.returncode:
            sys.exit('gcov-tool merge-stream failed on ' + path)
    count = sum(1 for f in os.listdir(out) if f.endswith('.gcda'))
    if not count:
        sys.exit('no .gcda file in %s after merging: the streams name another folder (merge on the PC that built the '
                 'instrumented NRO)' % out)
    r = subprocess.run([sys.executable, os.path.join(root, 'tools', 'pgo_summary.py'), out, str(len(files))])
    if r.returncode:
        sys.exit('pgo_summary.py failed')
    print('%d session(s) merged into %d file(s) in %s' % (len(files), count, out))


if __name__ == '__main__':
    main()
