# Rebasing nights-nx on the ultimate base: the inventory and the method

Written 2026-11-22, as the code moved (the user: "sync up with the ultimate base and do the inexact codegen stuff and
a census please for it"; the base's docs/rebasing-a-port-prompt.md is the method). The rebase starts from nights-nx
1.0.2 and the packed-files / Opus / H.265 work of 2026-11-22 (`master`, 65ec444e) and the ultimate base
(`D:\renut-ultimate\base`, branch `ultimate`, **2705e5bc**, ports 1-254). `master` stays as it is; the rebase is the
branch `ultimate` in the worktree `D:\nights\nights-nx-ult`. Nothing is pushed.

## The lineage

nights-nx's first commit (e1cdfa43) is banjo-nx's working tree of 2026-10-28 (its HEAD 1889dab6 and 41 modified
files). The base's first commit (83b4ae77) is banjo-nx 894d3256 of 2026-10-29 (which has 1889dab6 as an ancestor, and
banjo-nx's title update 3 work) plus 13 files; its 838c8c41 moved Renut's hooks and device layout to Beautiful
Katamari's XDK 2.0.5632; its ports 1-254 came from the other ports since. So nights-nx and the base share banjo-nx's
tree of 10-28/29: nearly all of nights-nx's general work is banjo-nx's, which the base carries in later forms. What
nights-nx added since its fork (`git diff e1cdfa43 master`: 188 files) is the inventory below.

`tools/nights_ult_classify.py` (banjo-nx-ult's tools/ult_rebase_classify.py's form) compares five trees, line ends
ignored: E (e1cdfa43), N (master), S (83b4ae77), M (838c8c41), T (the base's tip). Of 23,758 paths: 22,882 the same in
T and N; 251 the base changed and NiGHTS did not (taken from T); 444 new in the base; 84 changed on both sides; 3 that
NiGHTS changed and the base did not; 92 (plus ZXC's and LZ4's sources) new in NiGHTS; 2 the base deleted.

## The method: a full rebase

1. The base's whole tree, one commit ("The ultimate base at 2705e5bc": `git read-tree` of the base's commit, its 23,664
   blobs as they are; `git ls-files` counts the same as the base's).
2. Katamari's layer out, one commit.
3. Each piece of NiGHTS's layer, a commit each, through `tools/nights_ult_apply.py`: NiGHTS-only files as they are; a
   file changed on both sides merged three ways (ours N, theirs T, ancestor E; for the per-XDK files the ancestor is M,
   so the ports' later changes come in and Katamari's addresses stay out); each diff3 block resolved by hand
   (`tools/nights_ult_resolve.py`) and each merged file's diff against the base read, so that only NiGHTS's lines stay
   on top of the base's file.
4. Where the base has its own form of what NiGHTS did, the base's form wins and NiGHTS's is left out (list b). Nothing
   is grafted because it was there (list d).

## (a) NiGHTS's layer: grafted

| Piece | Where | Commit |
|---|---|---|
| Code generation config | app/renut_config.toml, app/config/{renut_funcs,renut_hooks,renut_nonarg_keep}.toml, switch_goto.txt, the hot / opt-out / choice lists (empty), app/indirect_promotions.txt: 1.0.2's as they are (the same codegen keys the base sets). The base's config/renut_crt.toml (an empty table) stays; Katamari's app/overrides.toml goes | 61376539 |
| Renut at XDK 2.0.21173 (Peggle 2's 21256 layout) | the per-XDK files (d3d.h, d3d_native.h, d3d_native_state.cpp, d3d/hooks/*, banjo_shared_hooks.h) with the ports' changes; per-XDK values the base wrote into shared files at Katamari's numbers, set to NiGHTS's: the viewport (port 138, +0x32E0, from NiGHTS's GetViewport), the device's own vertex declaration (port 213, +0x3118), the scaler copy (port 169, +0x3730), render/draw.h's field map, queued.cpp's private words (11008-12224) and 40 read chunks, draw.cpp's census tail. The snapshot's last chunk (32 bytes) the base computes from d3d.h | 8c827b14 |
| NiGHTS's Direct3D fixes | the automatic front buffer, the hardware block, BeginTiling / EndTiling in 1.0.2's form, the gamma ramp at +0x3C30, SetGammaRamp / SetPWLGamma with Flags, GetFunction at sub_82351100 / sub_82351158 (through port 196's body) | 8c827b14 |
| SMAA before the HUD | nights_renut_smaa_before_ps (NiGHTS has no final composite), on the base's SMAA path | 8c827b14 |
| Packed files, Opus music, VC-1 / H.265 movies | the SDK's lz4_blocks (rr6-nx's version 2 with ZXC), .adxo, libopus, the VFS index's packed hint; FFmpeg's VC-1 decoder; app/src/nights_wmv_ffmpeg.cpp, nights_adx_opus.cpp; tools/switch/nxvideo | 4261b752 |
| The game layer | src/nights_keys.cpp (the base's keys file), src/nights_frame_hooks.cpp (pacing at the game's interval, true 60 / 30 fps, the Saturn ground, the button codes), the built-in and player settings, renut_app.h's lines, app/CMakeLists.txt's lines, the icon, the forwarder | 13c69de8 |
| Hand natives and hot lists | NiGHTS's memcpy / memset / transforms / buffer reset in the base's banjo_hand_natives.h; 1.0.2's hand.txt, localized.txt, localized_bulk.txt | 13c69de8 |
| Tools | NiGHTS's values in the base's passes (inline_memcalls, leaf_dead_stores, lr / r1_preserve, post_codegen's switch_goto list); NiGHTS's analysis, remap and package tools; the rebase tools | 929152b0 |
| Docs | NiGHTS's records (docs/nights-notes.md, progress-log.md, package-notes.md, morning-summary.md) as written, with this plan, docs/ultimate-sync.md, docs/nights-ultimate-notes.md and docs/nights-near-exact.md | the docs commit |

## (b) General work the base already has: NiGHTS's form left behind

| nights-nx's piece (from which port) | The base's form |
|---|---|
| The SDK fixes of b54c6465 (peggle1-nx / zumasrevenge-nx / zuma-nx through their SDKs): no leaderboards (XUserReadStats fails), XamUserCreateStatsEnumerator, the host thread's FPSCR (InitHost), the bounce buffer for file reads on Horizon, the Joy-Con's sideways stick | the same fixes in the base's SDK (xgi_app.cpp, xam_user.cpp, xthread.cpp, filesystem_posix.cpp's piece size, switch_input_driver.cpp, live from the game menu) |
| sa2-nx's built-in config (LoadConfigString, BuiltInConfig / DefaultConfigFile) | the base's (rex_app.h, cvar.cpp: the same design) |
| nights_renut_double_buffer, nights_renut_queue_drain and the late input's guard (display_sync.*, video.cpp, render_thread.cpp) | pacman_renut_double_buffer, pacman_renut_queue_drain, LateInputGuardUs: the same code; NiGHTS's settings name the base's keys |
| The language key (src/nights_language.cpp, `language = "auto"`) | the SDK's `language` key (blur-nx's: names case-insensitive; "auto" is not a name and falls to System, the console's language, what 1.0.2's auto did) |
| The PC-build fixes of banjo-nx's 10-28 tree: helpers out of the deko3d-only block, the two RepeatCensusReport, `far` | the base's (draw.cpp's block, ReplayRepeatCensusReport, three_ahead), surfaces.cpp's PLUME_DEKO3D block |
| N&B's address hooks under `#if 0` (widescreen.cpp, photo_capture.cpp, mnk_controls.cpp's keystroke hook) and NIGHTS_NB_SOURCES | the base's KATAMARI_BANJO_ADDRESS_HOOKS (0) and its source list (N&B's files not built) |
| bejeweled3-nx's GetFunction (the record's `function` copy) | port 196's RenutShaderGetFunction and the record's `container` (banjo_renut_shader_get_function) |
| The game frame counted in nights_frame_hooks.cpp | Renut's Swap hook counts it (blur-nx's, d3d/hooks/device.cpp), Present ending in it |
| src/nights_dropped_cvars.cpp (126 N&B keys stubbed) | the base's keys file (Katamari's, now src/nights_keys.cpp) for what the shared code reads |
| The tools nights-nx had added from banjo-nx's tree (codegen.ps1, crt_signatures.py, pointer_fixpoint.py, switch_check.py, ppc_dis.py, renut_device_offsets.py, renut_shader_dump.py, ...) | the base's copies |
| tools/emu_run.sh, dist/emu_*.toml | the base's tools/run_ryujinx.ps1 |

## (c) General work the base lacks: grafted, with a handoff

| Piece | Grafted | Handoff |
|---|---|---|
| ZXC blocks in .lz4b (version 2), .adxo, the VFS index's packed hint (rr6-nx / culdcept-nx / sa2-nx-ult) | yes | sa2-nx-ult carries the same; the base's installer would need ZXC to make them |
| FFmpeg's VC-1 decoder and the WMV movies' decoder hook, nxvideo | yes (NiGHTS's hook) | the decoder line in codec_list.c |
| SMAA before a list of pixel shaders (a game with no final composite) | yes | yes |
| post_codegen.py's switch_goto list from app/config/switch_goto.txt (the base's line names N&B's interpreters) | yes | yes |

## (d) Left behind, and why

| Piece | Why |
|---|---|
| The interpolated 60 fps presentation (backend/frame_double.*, nights_present_60 and its keys, nights_diag_camera_frames; its hooks in queued.cpp, render_thread.cpp, video.cpp, device.cpp, draw.cpp) | off in 1.0.2 and out of its tomls since 1.0.1 ("no interpolation"); 1.0.2's 60 fps is the game's own (nights_true60), which is grafted |
| input_script_stick_cycle (sdk input_system.cpp) | a test tool; the base's input_script has stick pushes of its own (banjo-nx's 'LSx/y/seconds') |
| BeginTiling / EndTiling in the base's Katamari form (Flags, ClearZ, ClearStencil used) | 1.0.2 drew with N&B's form (Flags ignored, Z 0, stencil 0): pixel-identical first; the base's form is a picture decision with an A/B |

## The base's Katamari layer, which went (5f1cf47a)

app/katamari_switch*.toml, src/katamari_builtin_config.h.in, src/cpu/katamari_stage_diag.cpp, dist/switch/katamari-nx,
pgo/ (Katamari's profile), tools/katamari_files.py, katamari_shader_files.py, make_katamari_tomls.py, the hook and
device-offset remap tools and map, the forwarder files, the icon, app/overrides.toml. Katamari's keys file became
src/nights_keys.cpp. Katamari's hand natives stay in banjo_hand_natives.h, unlisted (they never compile).

## The order of work

1. This plan, the base's tree, Katamari's layer out, NiGHTS's layer in.
2. Code generation (one module: default.xex with the title update's default.xexp) and post_codegen.py in full.
3. The PC build to gameplay, the verify run, the calling-convention censuses.
4. The censuses of the not-exact forms (docs/nights-near-exact.md).
5. The Switch build, as disk space allows (D: had 8.8 GB free on 2026-11-22).

The progress is in docs/nights-ultimate-notes.md.
