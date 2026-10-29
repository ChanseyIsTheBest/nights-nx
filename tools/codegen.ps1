# nights-nx: translates the game (app/assets/default.xex + the TU's default.xexp beside it) to C++ with the code
# generator, outside the build. The PC build re-runs the generator by itself when an input changes (and appends
# tools/post_codegen.py); this script is for the analysis loop before the first build (tools/pointer_fixpoint.py calls it).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1 [-Rexglue <exe>] [-PostPasses]
#
# Output: app\generated (not committed), out\renut_image.bin (the loaded image; game data, not committed),
#         out\codegen.log
param(
    [string]$Rexglue = '',
    [switch]$PostPasses
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$app = Join-Path $root 'app'
if (-not $Rexglue) {
    $Rexglue = Get-ChildItem (Join-Path $root 'sdk\out'), (Join-Path $app 'out') -Recurse -Filter 'rexglue*.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^rexglue(rd|d)?\.exe$' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Rexglue -or -not (Test-Path $Rexglue)) { throw 'rexglue.exe not found' }
$Rexglue = (Resolve-Path $Rexglue).Path
New-Item -ItemType Directory -Force (Join-Path $root 'out') | Out-Null
$log = Join-Path $root 'out\codegen.log'
Write-Host "== codegen with $Rexglue"
Push-Location $app
try {
    $env:REX_MAX_JUMP_TABLE_ENTRIES = '4096'
    $env:REXGLUE_DUMP_IMAGE = Join-Path $root 'out\renut_image.bin'
    & $env:ComSpec /c "`"$Rexglue`" codegen renut_manifest.toml > `"$log`" 2>&1"
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
Get-Content $log -Tail 12
if ($code -ne 0) { throw "codegen failed (exit $code), see out\codegen.log" }
if ($PostPasses) {
    $env:REXSDK_DIR = Join-Path $root 'sdk'
    & python (Join-Path $root 'tools\post_codegen.py')
    if ($LASTEXITCODE -ne 0) { throw "post_codegen failed ($LASTEXITCODE)" }
}
