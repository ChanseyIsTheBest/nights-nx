#!/usr/bin/env python3
"""The censuses of one PC run's log (banjo_diag_runtime_census = true), as the base's tools/cg13_census.ps1 reads its
CG-13 lines, in Python (nights-nx, 2026-11-22: PowerShell scripts are not run past the execution policy on this PC),
plus the other not-exact forms' counters (docs/off-by-default.md section 2a):

  - CG-13 (BLUR_FPCR_ONE_MODE): per 10 s window, the FPU-mode regions with a denormal operand or an underflow and the
    any-mode cold paths; the verdict; the sites by function and generated line (tools/census_sites.py --denormal);
  - the guest's flush-mode switches a second (each an msr fpcr on the console that the one-mode build leaves out);
  - Blur's near-exact switches (the last cumulative line): #1, #2, #17, #41, #42, #3 / #4, #5;
  - fp_sign_ops' condition (FPSCR rounding mode up or down written by the guest);
  - #32, #38 and the other cumulative near-exact lines, as they are.

    python tools/nights_census_read.py out/pc_runs/<run>/<run>.log [--top 40] [--windows]
"""
import argparse
import os
import re
import subprocess
import sys

CG13 = re.compile(r'\[census\] CG-13 \(one flush mode\): per second (\d+) any-mode cold paths and (\d+) of (\d+) '
                  r'FPU-mode regions')
SWITCHES = re.compile(r'\[census\] guest code per second: (\d+) flush-mode switches')
NX = re.compile(r'\[census\] near-exact \(cumulative\): (.*)$')
ROUND = re.compile(r'\[census\] FPSCR rounding mode written by the guest \(cumulative\): (.*)$')
OTHER = re.compile(r'\[census\] (near-exact #\d+[^:]*\(cumulative\)[^:]*): (.*)$')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--windows', action='store_true', help='print every window\'s CG-13 numbers')
    a = ap.parse_args()
    windows, switches = [], []
    nx = rounding = None
    other = {}
    with open(a.log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = CG13.search(line)
            if m:
                windows.append(tuple(int(x) for x in m.groups()))
                continue
            m = SWITCHES.search(line)
            if m:
                switches.append(int(m.group(1)))
                continue
            m = NX.search(line)
            if m:
                nx = m.group(1)
                continue
            m = ROUND.search(line)
            if m:
                rounding = m.group(1)
                continue
            m = OTHER.search(line)
            if m:
                other[m.group(1)] = m.group(2)
    name = os.path.splitext(os.path.basename(a.log))[0]
    if not windows:
        print("No '[census] CG-13' line in %s (a run shorter than 10 s, or a build without the census)." % a.log)
        return 1
    hit = [i for i, (cold, den, _) in enumerate(windows) if cold or den]
    regions = sum(w[2] for w in windows) / len(windows)
    sw = sum(switches) / len(switches) if switches else 0
    print('CG-13 census, %s: %d windows of 10 s; about %.0f flush-mode switches a second in the game code (each an '
          'msr fpcr on the console, which the Switch build leaves out) and %.0f FPU-mode regions a second.'
          % (name, len(windows), sw, regions))
    if a.windows:
        for i, (cold, den, reg) in enumerate(windows):
            print('  window %3d: %d any-mode cold paths, %d of %d regions with a denormal' % (i + 1, cold, den, reg))
    if not hit:
        print('Verdict: 0 regions with a denormal and 0 any-mode cold paths in every window: the one flush mode '
              'changes no scalar result in what this run played.')
    else:
        print('Verdict: %d of %d windows met denormals (windows %s; at most %d regions and %d any-mode cold paths a '
              'second). banjo::FormsSelfCheck\'s sites are its own deliberate ones at start-up.'
              % (len(hit), len(windows), ', '.join(str(i + 1) for i in hit), max(w[1] for w in windows),
                 max(w[0] for w in windows)))
    if nx:
        print('Near-exact (cumulative, the last line): ' + nx)
    if rounding:
        print('FPSCR rounding mode written by the guest (cumulative): ' + rounding)
    for k, v in sorted(other.items()):
        print('%s: %s' % (k, v))
    if hit:
        sites = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'census_sites.py')
        sys.stdout.flush()
        subprocess.run([sys.executable, '-B', sites, a.log, '--denormal', '--top', str(a.top)])
    return 0 if not hit else 2


if __name__ == '__main__':
    sys.exit(main())
