#!/bin/bash
# nights-nx (from banjo-nx's emu_run.sh): one Ryujinx run of the SD card's NRO in this port's own data folder
# (D:\New folder\ryu-data\nights), with a toml made from a base toml plus extra keys; captures and the log are moved to
# out/emu/<name>. Only this run's process is stopped, by its PID (never Ryujinx by name: other ports run theirs).
# Waits for 13 GB of free commit memory first (another port's emulator can run the host out of memory).
#
#   tools/emu_run.sh <name> <seconds> [key = value ...]      NRO=<path> copies that NRO in first; BASE=<toml>
set -u
NAME=$1; SECS=$2; shift 2
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out/emu/$NAME"
DATA_W='D:\New folder\ryu-data\nights'
SD="/d/New folder/ryu-data/nights/sdcard/switch/nights-nx"
BASE=${BASE:-$ROOT/dist/emu_base.toml}
mkdir -p "$OUT" "$SD"
if [ -n "${NRO:-}" ]; then
  cp "$NRO" "$SD/nights-nx.nro" && md5sum "$SD/nights-nx.nro" | cut -c1-12
fi
# Free commit memory (GB) and no other run of ours.
for i in $(seq 1 20); do
  F=$(powershell.exe -NoProfile -Command "\$o=Get-CimInstance Win32_OperatingSystem; [int](\$o.FreeVirtualMemory/1MB)" | tr -d '\r')
  [ "$F" -ge "${COMMIT_GB:-13}" ] && break
  echo "free commit ${F} GB, waiting"; sleep 30
done
echo "free commit ${F} GB"
# The user's rule: 11-13 GB or more before a run. Still under the floor after the wait: no run.
if [ "$F" -lt "${COMMIT_FLOOR_GB:-11}" ]; then echo "emu run $NAME: not started (free commit ${F} GB)"; exit 2; fi
if powershell.exe -NoProfile -Command "if (Get-Process -Name Ryujinx -ErrorAction SilentlyContinue) { exit 1 }"; then :; else
  echo "emu run $NAME: not started (a Ryujinx is running: one run at a time)"; exit 2
fi
python - "$BASE" "$SD/renut.toml" "$@" <<'EOF'
import re, sys
text = open(sys.argv[1], encoding='utf-8').read()
for kv in sys.argv[3:]:
    k, _, v = kv.partition('=')
    k, v = k.strip(), v.strip()
    pat = re.compile(r'^%s\s*=.*$' % re.escape(k), re.M)
    line = '%s = %s' % (k, v)
    if pat.search(text):
        text = pat.sub(line, text)
    else:
        m = re.search(r'^\[', text, re.M)
        text = text[:m.start()] + line + '\n' + text[m.start():] if m else text.rstrip('\n') + '\n' + line + '\n'
open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write(text)
EOF
rm -f "$SD"/capturas/captura_*.png 2>/dev/null
rm -f "$SD/../rex_profile.log" "$SD/../rex_stderr.log" 2>/dev/null  # the SDK writes these at sdmc:/switch/
powershell.exe -NoProfile -Command "\$env:DOTNET_ROOT = 'D:\New folder\dotnet10'; \$env:RYUJINX_HOMEBREW_SHADER_CACHE_ID = '0500004E49474854'; \$nro = '$DATA_W\sdcard\switch\nights-nx\nights-nx.nro'; \$p = Start-Process -FilePath 'D:\New folder\Ryubing-src\ryubing\build\Ryujinx.exe' -ArgumentList @('-r', '\"$DATA_W\"', ('\"' + \$nro + '\"')) -WindowStyle Minimized -PassThru -RedirectStandardError '$(cygpath -w "$OUT")\stderr.txt' -RedirectStandardOutput '$(cygpath -w "$OUT")\stdout.txt'; 'pid ' + \$p.Id; try { Wait-Process -Id \$p.Id -Timeout $SECS -ErrorAction Stop; 'exited by itself' } catch { }; Stop-Process -Id \$p.Id -Force -ErrorAction SilentlyContinue; Start-Sleep 2"
for f in "$SD"/capturas/captura_*.png; do [ -f "$f" ] && mv "$f" "$OUT/"; done
L=$(ls -t "$SD/logs" 2>/dev/null | head -1)
[ -n "$L" ] && cp -r "$SD/logs/$L" "$OUT/" 2>/dev/null
for f in rex_profile.log rex_stderr.log; do [ -f "$SD/../$f" ] && cp "$SD/../$f" "$OUT/"; done
echo "emu run $NAME: $(ls "$OUT" | grep -c png) captures, log ${L:-none}"
