#!/usr/bin/env python3
"""One PC test run of app/out/pc/renut.exe, as the base's tools/run_pc.ps1 does it, in Python (nights-nx, 2026-11-22:
PowerShell scripts are not run past the execution policy on this PC). Minimised, muted, its own unbuffered log, PNG
captures every N seconds, stopped after --seconds; stops only the process it started (by its PID).

    python tools/nights_pc_run.py --seconds 120 --name boot1 [--every 15] [--game D:/nights/game]
        [--extra "--key=value ..."] [--visible] [--census] [--user <save folder to copy>]

--census adds the base's census keys (banjo_diag_runtime_census = true) and, after the run, the CG-13 verdict and the
near-exact counters (tools/nights_census_read.py). The log is out/pc_runs/<name>/<name>.log.
"""
import argparse
import glob
import os
import shlex
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=int, default=90)
    ap.add_argument('--name', default='run')
    ap.add_argument('--every', type=int, default=15)
    ap.add_argument('--extra', default='')
    ap.add_argument('--game', default='D:/nights/game')
    ap.add_argument('--build', default='pc', help='the folder under app/out')
    ap.add_argument('--visible', action='store_true')
    ap.add_argument('--census', action='store_true')
    ap.add_argument('--user', default='', help='a user-data folder to copy first (a save), into out/pc_runs/<name>/user')
    a = ap.parse_args()
    folder = os.path.join(ROOT, 'app', 'out', a.build)
    exe = os.path.join(folder, 'renut.exe')
    if not os.path.isfile(exe):
        sys.exit('no %s: build the PC folder first' % exe)
    logs = os.path.join(ROOT, 'out', 'pc_runs', a.name)
    os.makedirs(logs, exist_ok=True)
    log = os.path.join(logs, a.name + '.log')
    if os.path.exists(log):
        os.remove(log)
    user = os.path.join(ROOT, 'out', 'pc_user')
    if a.user:
        user = os.path.join(logs, 'user')
        if os.path.exists(user):
            shutil.rmtree(user)
        shutil.copytree(a.user, user)
    args = [exe, '--log_file=' + log, '--audio_mute=true',
            '--window_start_minimized=' + ('false' if a.visible else 'true'), '--fullscreen=false',
            '--game_data_root=' + a.game, '--user_data_root=' + user.replace('\\', '/'),
            '--nfsmw_captura_cada_s=%d' % a.every, '--nfsmw_captura_max=60', '--log_file_writer=false']
    if a.census:
        args.append('--banjo_diag_runtime_census=true')
    args += shlex.split(a.extra, posix=True)
    start = time.time()
    with open(os.path.join(logs, 'stdout.txt'), 'wb') as out, open(os.path.join(logs, 'stderr.txt'), 'wb') as err:
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0
        p = subprocess.Popen(args, cwd=folder, stdout=out, stderr=err, creationflags=flags)
        print('started pid %d' % p.pid, flush=True)
        try:
            p.wait(timeout=a.seconds)
            print('exited by itself with code %d' % p.returncode)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/F', '/PID', str(p.pid)], capture_output=True)
            p.wait()
            print('stopped pid %d after %d s' % (p.pid, a.seconds))
    time.sleep(1)
    for pat in (os.path.join(folder, '*.png'), os.path.join(folder, 'capturas', '*.png')):
        for f in glob.glob(pat):
            if os.path.getmtime(f) >= start - 1:
                shutil.move(f, os.path.join(logs, os.path.basename(f)))
    if os.path.exists(log):
        with open(log, encoding='utf-8', errors='replace') as f:
            tail = f.readlines()[-15:]
        sys.stdout.write(''.join(tail))
    if a.census:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'nights_census_read.py'), log])


if __name__ == '__main__':
    main()
