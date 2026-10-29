# nights-nx: the night of 2026-10-28/29 (summary for the user)

Details: `docs/nights-notes.md` (every finding and fix), `docs/progress-log.md` (the timeline), the package's
`PACKAGE_NOTES.md` (what to test).

## What works

- **PC** (`app/out/pc-nx/renut.exe`, reNut's renderer on Vulkan): boots through the ESRB / epilepsy screens, the SEGA
  and Sonic Team movies, the intro, the title, the menus (the first boot's game data created by itself), Spring Valley
  gameplay, Night Over, the Dreams screen; 30.0 fps paced like the console. Full game (licence), saves.
- **Emulator** (Ryujinx, `ryu-data\nights`, deko3d): the same route with the final optimised NRO; every verify
  check 0 differences, the CPU guard 0 differences on ARM (857,530 + 1.92 M calls); deko3d fast path 100 % of draws;
  26-30 fps in gameplay.
- **Release package** `dist/package/nights-nx_2026-10-29_1115/` (NRO md5 19f68a19..., commit d068edab): SMAA, 720 handheld / 1080 docked, double buffering with its queue drain; 60 fps shelved (your call). Before it: `dist/package/nights-nx_2026-10-29_0840/` (the first hardware test): the PGO + LTO NRO, the three tomls, configs for the picture options
  (60 fps, SMAA, banjo's resolutions), the four-core NPDM, the ELF + map, the sources (git archive + the generated
  code, which is derived from your game: keep it local), the shader caches from the emulator, PACKAGE_NOTES.md.

## What was done (the seven steps and the extras)

1. Codegen (XDK 21173 + title update), PC boot to gameplay.
2. Renut's Direct3D layer at NiGHTS's addresses (335 hooks), deko3d shaders (99), the Switch NRO.
3. Emulator checks: no visual difference left (the SEGA squares are the movie's own).
4. banjo-nx's enhancements as it ships them (SLOWER ON THE CONSOLE ones off); codegen `elide_lr_stores` and
   `non_argument_as_local` after their audit; inline memcpy / memset at NiGHTS's addresses.
5. Hot functions: 5 hand-written natives (memcpy, memset, two matrix-stack transforms, a float-buffer reset), 99 bulk
   localized copies (the ARM profile's top 100); guard: 0 differences on PC (70 M calls) and ARM.
6. PGO + LTO (the profile retaken at 07:53 from the final code: a build against a stale profile froze at boot).
7. The package.
- **60 fps on the screen** (`nights_present_60`, off; `configs/renut_60fps.toml`): the renderer draws each game frame
  again with its vertex shader constants moved half a frame (predict, no added latency) or halfway back (interpolate,
  +16.7 ms); adaptive (pauses where the render thread cannot keep up, so the game never slows down).
- **True 60** (the game's logic at 60): investigated, not possible as a setting (see below).

## Measured

- **Emulator (Ryujinx; a proxy, the console decides)**: gameplay 29.6 fps, render thread 6.8-7.8 ms a game frame,
  about 100 draws a frame; deko3d fast path 100 % of draws; snapshot deltas verified (31,000 draws a 10 s window,
  0 differ). Main thread ARM profile (before the natives): sub_822DD9F8 23 %, memcpy 22 %, a critical-section funclet
  6 %, the two matrix-stack transforms 4 % + 2 %; the second game thread: the float-buffer reset 26 %.
- **CPU checks**: guard 70 M + 58 M calls compared on the PC (two codegen rounds), 857,530 on ARM: 0 differences;
  the generated-code self-checks (r1 mirror, flush-mode flow, folded constants, critical-section owners) 0 differences.
- **Movies**: 2-10 fps in Ryujinx (the WMV decoder thread busy ~70 % of the samples).
- **PGO + LTO**: text 34.5 -> 35.35 MB (the profile's inlining; one 450 s session: Spring Valley, both Dreams screens,
  Elliot's stage).
- **60 fps**: PC 284-300 of 300 game frames doubled at 29.4-30.0 fps (the game's speed kept). Emulator (deko3d,
  emuG): the game holds 28-30 fps with it on; doubling runs at 300 of 300 frames in lighter scenes and pauses itself in
  heavy ones (where the extra frame costs 14-29 ms). The final NRO (emuI): 28.6-30 fps throughout, verify 0.
- **True 60**: `nights_true60_interval = 1`: 49-60 fps with everything frame-counted twice as fast (the ESRB screen
  3 s instead of 6).

## What is next

- **Movies on the console**: the WMV decoder plays at 2-10 fps in Ryujinx (CPU-bound, one thread). A host decoder
  (the SDK's FFmpeg already builds WMV3) behind the game's movie player is the fix; a project of its own.
- **True 60**: one logic update per frame with fixed per-frame steps in every task; a true 60 means halving each
  task's steps (a port of its own, task class by task class).
- A console run of the package (the A/B, the profile, 60 fps's cost).

## Your decisions

- The forwarder's program id (`0500004E49474854` in `renut.toml`), and whether to make the forwarder (four-core mode).
- 60 fps (`configs/renut_60fps.toml`), and predict vs interpolate (`nights_present_60_mode`).
- SMAA, banjo's resolutions / dynamic resolution (configs/).
- `headless = true` (the first boot's "Create Game Data" answered by itself) and `license_mask = 1` (the full game).
