# Off by default: what is there, and why it is off

The base ships switches that are off by default although they help somewhere: a fix that one game needs but that
breaks another game's picture, a faster form that is not exact, a speed-up the console measured as slower or as no
gain, a trial the emulator stalls with, or (section 5) an exact fix or accuracy option a port brought from another
port, off at the user's word until a game turns it on. This page sorts them by why they are off. Every table has a **Speed**
column: what is known about the switch's speed, and **Slower** where a console run measured it slower.

**The rule (the user, 2026-11-11):** a key that breaks the graphics of a specific game is OFF in the base by default.
The game that needs it sets it in its own toml (or CMake / environment, as below), after checking its own picture.

How to turn one on:
- a key: `name = true` in the game's toml (the in-run A/B list switches many of them during a run);
- a CMake option: `-DNAME=ON` on a build folder (an existing folder keeps its cached value);
- an environment variable at post-codegen: `NAME=1 python tools/post_codegen.py`, then build.

## 1. Fixes for one game that break another game's picture

Correctness fixes, and one speed-up (port 161's copy runs): off because they break a game the base also serves.

| Key | What it does | Why it is off | Speed | Who turns it on |
|---|---|---|---|---|
| `sasrt_renut_depth_restore` (port 29; port 202) | A depth target bound over EDRAM a 32-bit colour target wrote since takes its words as its depth, as on the console. | It drew Sonic 4's title sky white (S4ep1-nx: its reflection pass draws a resolved depth back as colour into EDRAM the main pass's depth shares); its PC A/B with it off gives the console's picture. Katamari never reaches it | A fix, not a speed-up | sasrt-nx |
| `RENUT_ALPHA_TEST_UNORM8` (define in shader_common.h; port 209) | The alpha test compares the alpha and the reference on the 8-bit grid (rounded times 255). | A picture change for every game with an alpha test; culdcept-nx's models (ALPHAREF 255 against a white vertex colour) needed it on the PC | A fix, not a speed-up | culdcept-nx (1) |
| `blur_renut_wipe_overwritten` (A/B 218, port 76) | vivapinata-nx's EDRAM wipe (a clear through another layout, applied at the wiped target's next resolve) by exact tile spans, and skipped when another view over the same tiles drew or cleared after it. Blur's frame came out black with the old rule. | vivapinata-nx (2026-11-11), against the SDK's emulated GPU: its fade frames at the attract mode's cuts came out 5-8 % white (fog) with it on, 0 % off and on the reference. The case it misses: a clear through another layout at the same EDRAM base that covers every tile of the mask's target. Off = vivapinata-nx's rule (port 77). | A fix, not a speed-up | blur-nx |
| `banjo_renut_replay_inherits` (halo3-nx) | A replayed command buffer draws with the running device plus only what its recording set, as on the console. Off, each recording's whole device is used, and a recording on a device of its own (Halo 3's no-hardware device) has no targets, so its draws are dropped. | vivapinata-nx (2026-11-11): a hard shadow under its title logo with it on (2,378 dark grass pixels at 21 s), 0 off and on the emulated GPU; its mask's normal frames 5-9 % white on, 2.4-5.9 % off (the reference 2.5-5 %). Port 67's set_words fix does not cure it. Off since port 77. | A fix, not a speed-up | halo3-nx, and any game that records on a device of its own |
| `blur_renut_edram_inherit` (A/B 220, port 76) | Before a draw that keeps part of its target (a partial write mask, or blending), the target takes what other views wrote over its EDRAM tiles since (a full-target copy draw; Blur's motion-blurred frames). | vivapinata-nx (2026-11-11), on the console: a band of garbage tiles over the top 960x48 pixels of its title screen (12x3 EDRAM tiles, dark scrambled scene content), at first entry and after every attract fly-through; off, none; the PC (D3D12) shows none either way, so it is the deko3d side (port 76 added the multisampled source's bind there). Off since port 86. | A fix, not a speed-up (it adds a copy draw) | blur-nx |
| `banjo_deko3d_copy_run_barriers` (port 161; off since port 250) | After a Full barrier, no barrier between back-to-back buffer copies (deko3d's copy engine was taken to run them one at a time). Off: a barrier before every copy, as before port 161. | blur-nx (2026-11-21), its console bisect, one Barcelona Oval race each, then release-NRO burst races: with it on, rainbow colour fringes on the car, the mirror and the scenery (the bug's signature) in 4 of 5 races, among them the release NRO as players run it (the installer's GLSL package, a warm DKSH cache: 1 shot of 42 with strong fringes); off, none in 4 of 4, among them two release-NRO burst races of 38 and 42 shots. A dithered, grainy car (the number plate a white blob) is Blur's own dissolve effect, seen with it off too. A player's blur-nx issue reports it in the release (the car blurry, warping, glitching). The NRO's own GLSL package and an empty DKSH cache make it show reliably; the two packages' shaders are the same in logic, so the trigger is timing (shader compiles beside the race), not shader content, and every port is exposed. Why is not known (blur-nx's guess: copies that depend on one another, or on GPU writes since the last Full barrier, are not ordered without the barrier). This is the ghosting port 181 blamed on the texture park | A speed-up, not a fix: fewer gpfifo entries (each Full barrier is 2; fnxf_xiii had 675 copy-kernel draws fenced before and after). Off adds them back; port 157 sends a refused submission again in parts. The frame time with it off is not measured | none here; a game that wants it checks its picture with an empty DKSH cache and its NRO's own GLSL package first |
| `banjo_renut_blend_skip` (banjo_gpu_blend_skip) | Skips blending where it cannot change the pixel. | Off since banjo-nx package 30: it broke the depth of field (with banjo_gpu_bool_spec) and the deko3d renderer's river water in Banjo. | A speed-up (GPU), not measured since | none so far |

## 2. Faster forms that are not exact

Speed-ups that change results, so they are off unless a game has checked them.

| Switch | What it does | Why it is off | Speed |
|---|---|---|---|
| `BANJO_GEN_LR_MOVES_ELIDE`, `BANJO_GEN_NV_SAVES_ELIDE`, `BANJO_GEN_BACK_CHAIN_ELIDE` (CMake) | tools/frame_traffic.py's three stack-frame elisions for all game code: no guest link register moves, no saves of non-volatile registers that are locals, no back-chain stores. | They change the stale data a guest stack frame holds; sa2-nx found a game that reads a frame word it never wrote (characters tinted). | Faster (fewer stores per call) |
| `SASRT_HOT_LR_ELIDE` (CMake, port 74; off since port 84) | The link register's half of those elisions for the 12 bulk machine-copy shards only (sasrt-nx's idea 47). | vivapinata-nx's guard found 8 differences from it: the save elided in one of a copy's forms and not the other (forms in different translation units, only one with the define). Turn it on only after a guard run with it shows 0 differences. | Faster copies (sasrt-nx measured it with its round A batch, not alone) |
| `BANJO_GENERATED_NO_TRAPPING_MATH` (CMake) | -fno-trapping-math on the generated game code. | Not exact (see app/CMakeLists.txt). | Not measured |
| `BANJO_SWITCH_EARLY_RA` (CMake) | Leaves GCC's AArch64 early register allocation pass on. | It miscompiled the XDK's streaming copy in peggle2-nx (large textures came out as garbage on the console only). | GCC's default; no gain measured |
| `renut_format_7e3_rg11b10` (port 75) | Xenos 7e3 targets as 11:11:10 float, half FP16's bytes. | No alpha (the 7e3's two bits read as 1), one mantissa bit less on red and green, two on blue. | Less memory and bandwidth for such targets; sasrt-nx has none to measure |
| `renut_depth_float_no_stencil` (port 75) | Xenos float depth targets (D24FS8) as 32-bit float depth without stencil, half the bytes. | Stencil on those targets then does nothing (Katamari's scene depth is D24FS8 and uses stencil). | Less memory and bandwidth for float depth; not measured |
| `sasrt_texture_long_idle_frames` (port 58; an integer, 0 = off) | Releases host textures no draw bound for N frames. | Known to hang the GPU on the Switch (sasrt-nx, 1800). | A memory saver, not a speed-up |
| `vivapinata_depth_resolve_skip_unread` (port 115; A/B 156) | A multisampled depth resolve into a destination nothing has read yet is not drawn; the first read of it draws the one left out and every later one. | NOT exact in principle: when the target changed between the resolve left out and the first read, that one read sees the newer depth. Viva Pinata samples its copies, so it found nothing to skip there; for a game that really leaves depth resolves unread. (Viva's "[trace] sample resolved" line prints only for GPU-written textures, so it misses depth resolve destinations: not a test for "nobody reads this") | Not measured |
| `audio_switch_pump_pairs` (port 135; blur-nx's M-43) | The Switch audio pump asks the title for two frames back to back every 10.7 ms, the second rendered with the first's state still in the caches. | +5.3 ms of audio latency and the title's parameter updates every 10.7 ms instead of 5.3: a latency choice per game (blur-nx's user took it). | blur-nx's console: +3.5 % fps alone, +0.6 % on top of its stereo mixing |
| `banjo_xma_frame_cache_hash_only` (port 139; blur-nx's near-exact #12) | XMA frame cache nodes matched on their 64-bit key (two CRC32C streams of the packet and the previous one), the format and the sizes, without comparing the packets' bytes. | NOT exact: a key collision (about 2^-64 a lookup) would answer another frame's samples; the cache's report counts the collisions the byte compare finds (0 in blur-nx). | Not measured apart |
| `BLUR_LOCALIZE_ABI_TRUST=1` (environment at post-codegen, port 150; blur-nx's #232) | The machine copies take the PowerPC ABI's non-volatile registers (r14-r31, f14-f31, v14-v31, v64-v127, cr2-cr4) as a real call left them, without reloading them. | Near-exact: a callee that breaks the ABI (hand-written assembly) is the case it changes; off as every near-exact switch (port 140). | Part of Blur's codegen batches (+10 % with the binds), not measured alone |
| `BLUR_NEAR_EXACT` (CMake, port 140; blur-nx's near-exact switches; `BLUR_NX_ONLY`, `BLUR_NX_SKIP`) | REX_NX_SNAN_PASS (#1), REX_NX_DIV_NO_CHECK (#17), REX_NX_FCMP_NO_UN (#41), REX_NX_XER_SO_ZERO (#5), REX_NX_NO_TRAPS (#19: a trap only logs); REX_NX_FCTI_NO_NAN (#2) and REX_NX_VMINMAX_FAST (#42) only by `BLUR_NX_ONLY`. | NOT exact in rare cases, which differ per game: the PC census (`banjo_diag_runtime_census`, the "[census] near-exact" lines) counts each. blur-nx's took #2 (NaN conversions, 80 a second) and #42 (4 % of vector max/min lanes differ) out. A compile-time switch: retrain the PGO profile. | blur-nx's console: part of its +5.6 % (27.88 against 26.39 fps) with its other pieces |

(Port 72's VMX estimates and fused multiply-add are not exact either, but are ON at the user's word: within 1-3 ulp of
the exact forms, no further from the 360 than before; sasrt-nx's race with them and a fresh profile was +2.4 % fps.
`SASRT_VMX_FAST_ESTIMATES=OFF` / `SASRT_VMX_FUSED_MADD=OFF` give the exact forms. sasrt-nx's idea 46, building
write-once globals into the machine copies, is not in the base at all: sasrt-nx found it unsound.)

CG-13, `BLUR_FPCR_ONE_MODE` (CMake, port 123, blur-nx's), is not exact either, and is **ON in the Switch build** since
port 133 at the user's word ("it's universal big CPU benefit"); the PC build keeps it OFF. It runs all guest float
code in the host's flush mode: no `msr fpcr` between VMX and scalar code (Katamari's game code switches about 1.2
million times a second), and the any-mode helpers as the plain operations. A scalar operation that meets or makes a
denormal then gives zero, where the Xenon's scalar FPU keeps it (its VMX flushes as before). The PC build, whose mode
still switches, counts those results: `tools/cg13_census.ps1` runs it with `banjo_diag_runtime_census` and gives a
verdict, with the sites when there are any (tools/census_sites.py --denormal). Katamari: 0 in every window. Blur: two
thirds in job-queue code whose results the VMX flushes anyway, no visible or audible difference in four races;
blur-nx's console +4.6 % fps (21.13 -> 22.10, ABBA), CPU 155 -> 147 ms a frame. A game whose census shows a site that
matters (a division or normalisation of the value, a guard against zero, a replay's simulation) builds its Switch
folder with `-DBLUR_FPCR_ONE_MODE=OFF`. A compile-time switch: a PGO profile is trained with the setting it ships with.

### 2a. How to check them on a game

One PC run counts what most of these forms would change in a game. The counters are in the PC build only
(`REX_DIET_CENSUS_ON`; the Switch build compiles them out, so neither the console nor Ryujinx can count). The PC build
keeps CG-13 off, so the float mode still switches there and each switch can be checked.

1. Build the PC folder as usual (`tools\build_pc.bat`). `tools/cg13_census.ps1` refuses a folder configured with
   `BLUR_FPCR_ONE_MODE=ON`.
2. Play gameplay with the census on, not only menus. In Ridge Racer 6 every denormal came in the race. Either
   `powershell -File tools\cg13_census.ps1 -Seconds 420 -Script "<the game's input script>"` (or `-Visible` to play it
   by hand), or the game's own scripted run with `--banjo_diag_runtime_census=true` added, then
   `tools\cg13_census.ps1 -Log out\pc_runs\<run>\<run>.log` for the verdict.
3. Read the log's `[census]` lines (each 10 s; the near-exact ones are cumulative, so the last one counts the run):
   - **CG-13** (`[census] CG-13 (one flush mode)`): the verdict and, when a window met a denormal, the sites by
     function and line (`tools/census_sites.py --denormal`). `banjo::FormsSelfCheck`'s sites are its own deliberate
     ones at start-up. For each game site, look at what is done with the small value. A value that only decays, or goes
     on into VMX code (the 360's VMX flushes it anyway), is harmless. A division, a normalisation, a comparison with
     zero or a replay's simulation is not. If one matters, build the Switch folder with `-DBLUR_FPCR_ONE_MODE=OFF` and
     train its PGO profile again.
   - **Blur's near-exact switches** (`[census] near-exact (cumulative)`): #1 signalling NaNs at float copy stores, #2
     NaNs into float-to-integer conversions, #17 divisions by zero and INT_MIN / -1, #41 float compares with a NaN, #42
     vector max/min lanes that would differ, #5 compares copying XER[SO] = 1. 0 for an item means `BLUR_NEAR_EXACT`
     (or `BLUR_NX_ONLY` for #2 and #42) would change nothing the run played.
   - **fp_sign_ops' condition** (`[census] FPSCR rounding mode written by the guest`): `tools/fp_sign_ops.py`
     (`BANJO_GEN_FP_SIGN_OPS`, ON) is exact only while "up or down" stays 0. A negation the compiler folds into the
     arithmetic next to it is exact in round-to-nearest and toward-zero, not in round-up or round-down. If the count is
     above 0, find what runs while that mode is set, or build with `-DBANJO_GEN_FP_SIGN_OPS=OFF` (the integer forms
     back, `REX_FP_SIGN_INTEGER`; no new code generation needed).
   - `[census] near-exact #32`: the words guest compare-and-swaps wrote, and how many were written by one thread only.

Not counted by anything yet:
- **Port 72's VMX estimates and fused multiply-add** (`SASRT_VMX_FAST_ESTIMATES`, `SASRT_VMX_FUSED_MADD`): AArch64
  only, and the PC computes the exact forms, so no PC run sees a difference. What is known: sasrt-nx's console fuzz
  (64 M lanes each) put vrsqrtefp within 3 ulp and vrefp within 1 ulp of exact, and the fused forms differ in 2.4 %
  of lanes (one rounding, as the 360's own vmaddfp; the 360's own estimates are good to about 1/4096). A game's
  exposure can be counted statically: each generated statement carries its guest instruction as a comment (`// vrefp`,
  `// vrsqrtefp`, `// vlogefp`, `// vexptefp`, `// vmaddfp`, `// vnmsubfp`, and their 128 forms) in
  `app/generated/*.cpp`.
- **#232 (`BLUR_LOCALIZE_ABI_TRUST`), the stack-frame elisions, `SASRT_HOT_LR_ELIDE`, -fno-trapping-math and early
  register allocation:** no census. For the machine copies' forms a guard run compares them with the generated
  functions (`SASRT_HOT_GUARD_FORMS`, docs/building.md).

Ridge Racer 6 (rr6-nx, 2026-11-22; Lakeshore Drive, about 170 s of race): CG-13 0 denormals in the menus. In the race at
most 2,765 of about 320,000 FPU-mode regions a second (0.85 %), 0 any-mode cold paths. 92 % of them were tiny rotation
angles going into a sin / cos rotation builder whose matrices are multiplied in VMX code, so CG-13 stays on. Every
near-exact item 0 (#42: 0 of 18,460). Rounding up or down written 25 times during the menus and the race load (none in
the race), through the C runtime's float control from math-library code: fp_sign_ops' condition is not met, and what
runs in those windows has not been traced yet (at most an ulp, in a few library calls). Its code has the VMX estimates
and fused multiply-adds in 82 functions.

## 3. Speed-ups the console measured

All exact. Off because they did not pay on the console, not because they break anything. A game whose frames are
different may measure them again.

### 3a. Slower on the console

| Key | What it does | Speed |
|---|---|---|
| `pacman_vblank_display_sync` (port 97) | The display's refresh starts the game's frames (pacman-nx's, from futari-nx): the game waits at its Swap for the refresh, so frames do not queue up behind each other. Needs `banjo_display_sync`. | **Slower in a game under the refresh rate:** blur-nx's 4-core console races (2026-11-12, 240 s each): 17.67 fps with it, 20.83 without (+18 %), input -> present 60.7 -> 60.4 ms (no latency cost); its Game Master waited in the pacing wait for 13-16 % of its samples (frames held to whole refreshes: 3 or 4, 20 or 15 fps), 1.4 % without. It fixed pacman-nx's input lag at its full frame rate: pacman-nx turns it on in its toml (2026-11-12, the user's call) |
| `banjo_renut_shadow_sort` | Depth-only draws of a recorded command buffer replayed sorted by shader. | **Slower:** render thread +1.1 ms, GPU +0.65 ms a frame (banjo-nx's console run of 2026-10-27) |
| `banjo_renut_replay_lookahead` | The next replayed op's lookups prefetched while a draw is drawn. | **Slower:** render thread +2.15 ms a frame (2026-10-27) |
| `banjo_renut_replay_stream_shaders` | The shader memo's sets prefetched two ops ahead instead of one. | **Slower:** +0.7 ms a frame, 4 of 5 periods worse (run c4, 2026-10-27) |
| `banjo_renut_texture_slot_skip` | A draw that changes some texture slots keeps the others without a full FillTextures. | **Slower:** render thread +0.64 ms a frame (night_final, 2026-10-28) |
| `sasrt_gpu_tiled_cache` (A/B 102) | Maxwell's tiled caching (binning), which NVN uses and deko3d leaves off. | **Slower:** +3.96 ms of GPU a frame in vivapinata-nx's garden (19.0 -> 17.7 fps, in-run A/B, run c7, 2026-11-12; its per-item GPU times read faster, but binning moves the work past their timestamps: the frame rate is the measure); sasrt-nx found it no better in its race. The picture was not judged tw2de_nx runs it on: its console, prologue 23.7-23.8 -> 24.9-25.2 fps, GPU 40.4 -> 35.8-36.8 ms a frame (its RGBA16F and 64-bit depth passes are bandwidth bound); kept off here by these two measurements (port 145, 2026-11-16); N&B: GPU +12.5 % a draw (banjo-nx's console c44, 9 of 9 pairs) |
| `banjo_deko3d_command_stage_float` (port 251, A/B 189) | The command stage and the warm helper float on cores 1-3 instead of the system core. | **Slower:** render thread -1.5 % but all threads' CPU +2.5 % and fps -3.3 % (N&B, banjo-nx's console c44: the game's threads lose time on cores 1-2) |
| `banjo_deko3d_command_stage_stream_ahead` (port 251, A/B 188; 0 = off) | The command stage prefetches its stream's lines ahead. | **Slower:** GPU +0.4 %, no render-thread gain (N&B, c44) |
| `banjo_deko3d_cached_commands` (port 251, A/B 173 without the stage) | Command memory CPU-cached and cleaned at each list's end by the render thread. | **Slower** where the render thread sets the frame rate: render thread +0.1 % and +0.9 % a draw, GPU -1 % (N&B c36, c39); with the stage, banjo_deko3d_cached_commands_with_stage (on) does it |
| `banjo_deko3d_kick_submit` (port 251, A/B 179; off on the Switch) | Lists ended and submitted at the game's kicks. | **Slower:** render thread +1.7 % a draw, fps -2.3 % (N&B, c44); a latency trade: sasrt-nx keeps them on in its settings |
| `banjo_renut_replay_shader_behind`, `banjo_renut_replay_copy_prefetch`, `banjo_renut_replay_card_prefetch`, `banjo_renut_replay_object_prefetch` (port 251, A/Bs 174-178, 181) | Render-thread prefetch groups (prefetches only). | The ones banjo-nx's in-run A/Bs measured as costing are off (N&B, 2026-11-21) |

### 3b. No gain on the console

| Key | What it does | Speed |
|---|---|---|
| `banjo_renut_replay_record_prefetch` | The replay prefetches the next op's shader and declaration records. | Neutral: -0.21 ms a frame, within the noise (night_gpu5) |
| `banjo_renut_host_packets` | A replayed draw's derived state kept by its device state and reused. | Neutral: -0.54 ms and +0.45 ms in two runs (night_gpu5) |
| `banjo_renut_merge_draws` | Consecutive draws that change nothing merged into one host draw. | No effect: Nuts & Bolts has no such draws (the census) |
| `banjo_renut_inline_line_compare` | An older compare of the pushed shared constants. | No effect since 2026-10-23 (the mask compare replaced it); before that it was **slower**: PushSharedConstants 2.7 -> 4.9 % of the render thread (36q) |
| `switch_thread_priorities`, `switch_thread_cores` (port 109; empty) | A host priority or core mask for a thread by its name's prefix, set when the thread names itself (`"Game Master=0x3A,JQ Worker 0=0x3A"`; masks `NAME=0xC`). A raised thread is not time-sliced with the game's other threads at 0x3B. | No gain in blur-nx: Game Master and JQ0-2 at 0x3A, 23.47 fps against 23.84 (its four cores full, 377-394 % of 400: the frame follows the CPU); a switch for other games' A/Bs (2026-11-12) |
| `sasrt_gpu_lazy_depth_resolve` (A/B 104) | A written compressed depth target decompressed right before something reads it, not each time it leaves the bind point. | No difference: GPU 56.24-56.29 ms on against 56.23-56.29 off (vivapinata-nx's garden, run c7, 2026-11-12) |
| `rr6_frames_ahead = 2` (port 138; blur-nx's #37) | The game may run two frames ahead of the render thread instead of one. | No gain in blur-nx: 27.52 against 27.54 fps (its console, 2026-11-13) |
| `audio_switch_pump_frames` (port 152; blur-nx's #196; 0 = off) | 1-4 audio frames back to back per pump tick (port 135's pairs widened); each frame past the first adds 5.3 ms of latency. | No gain in blur-nx at 3 |
| `thread_semaphore_address_wait` (port 148; blur-nx's audit #10) | A thread blocked on a guest semaphore sleeps on an address, as an event's does (port 65), and a release hands each unit to a blocked thread first. | No gain in blur-nx: 30.47 fps against 30.55 with the batch's others on (one race) |
| `thread_event_spin` (port 148; blur-nx's audit #13; 0 = off) | A wait on an unsignalled event watches it this many CPU yields before it blocks. | Noise in blur-nx: 30.66 fps at 200 against 30.55 (one race); it trades CPU time |
| `switch_yield_wake_on_progress` (port 139; blur-nx's #34) | The yield backoff's sleep ends early when the title reports progress (`RexProgressBump`, a hook the title must call). | No gain in blur-nx: 27.73 against 27.63 fps over 6 races each, within the noise |
| `banjo_renut_texture_park` (port 168; off since port 181) | Keeps a replaced texture (up to 64, 96 MB) and gives its image back when the same address asks for the same layout and content hash: no upload again. | No gain in Blur (0 take-backs from boot to 60 s of racing, its console, 2026-11-18) or Katamari (no "[park]" line). The artifact once blamed on it (ghosting, colour fringes and grain) was port 161's `banjo_deko3d_copy_run_barriers` (Blur's bisect of 2026-11-21; off since port 250, section 1); the park was off in those builds. A game that uploads again textures it had turns it on (fnxf_xiii: 35 uploads of 36.7 MB in one frame) |
| `banjo_deko3d_uam_pressure` (port 163) | uam's register pressure passes (another DKSH cache file). | No gain in Blur (its console, uncapped Barcelona, its shader cache filled): 30.63 and 31.16 fps against 31.19 without, GPU 29.84 and 29.21 ms a frame against 28.94 |

## 4. Trials: off until tried on the console, or because the emulator stalls with them

| Key | What it does | Why it is off | Speed |
|---|---|---|---|
| `vivapinata_shader_noearly` (port 95; A/B 151) | Renut's early alpha test skipped (constant 976 = 0, GLSL pass noearly): a quad the early test would discard is discarded by the shader's own alpha test instead. The same pixels. | **Per game:** it pays where the shader after the test is short; with long bodies and many quads killed early it is slower. Each game measures A/B 151 on the console and lists its slower shaders in tools/noearly_keep.txt (2026-11-12). | vivapinata-nx's garden: -1.66 ms a frame with its keep list (39.45 -> 37.78, run c24, A/B 151, stock clocks; its first run, c18, -1.30 ms, had the A/B numbered 91, whose off half also switched sasrt_gpu_precise_depth_resolve off); two of its shaders +0.28 and +0.06 ms without the test (they keep it) |
| `audio_xma_kick_without_lock` (port 120) | An XMA kick (XMAEnableContext) sets the context's flag without its lock, so the game's audio thread does not wait for a decode in progress; the decoder takes the kick with one exchange, a kick racing a clear keeps its bit. The same frames in the same order. | Sent off by blur-nx (its user had it compared both ways); a console trial per game. | blur-nx's console: +5.9 % fps in its race (locked 18.52 / 18.15, lock-free 19.36 / 19.48, the same NRO in ABBA order; Renut's wall-clock 10 s lines), the decoder's mutex unlock 0.6 ms a frame before |
| `BLUR_LOCALIZE_PROMOTED_MEMBERS=1` (environment at post-codegen; port 136, blur-nx's M-25) | Promoted indirect-call targets allowed into the machine copies' closures. | Parked by blur-nx: with it on its guarded PC boots hung, and copies of promoted targets wrote back r12 where the original leaves it (harmless by the ABI, but the guard flags it). | Not measured |
| `audio_xma_kick_wake_idle_only` (port 139; blur-nx's #65) | An XMA kick signals the decoder's worker only when it waits (a seq_cst handshake; the worker checks for pending work again before it waits). | Not measured on a console yet. | Fewer wake-ups of the worker |
| `switch_yield_spin` (port 139; blur-nx's #26; 0 = off) | The first `switch_yield_spin_first` yields of a quick run spin this many `yield` instructions instead of a kernel sleep. | Timing only; not measured on a console yet. | Fewer kernel calls on yield-heavy threads |
| `banjo_renut_ring_spin_us` (port 249; banjo-nx's experiment #58; 0 = off) | With the ring empty, Renut's render thread looks again for up to this many microseconds (a `yield` between looks) before it sleeps. | Spends the render thread's core while it looks; not measured on a console (banjo-nx left it off). | A push in that time skips the kernel's sleep and wake (tens of microseconds each way) |
| `banjo_display_sync` | A thread follows the display's vsync; the frame limiter releases frames on it. | In Ryujinx every run with it stalled or hung (banjo-nx runs 65, 67, 68); for a console trial. | A latency change; not measured on the console |
| `banjo_renut_core_layout` | Pins the drawing thread to core 0 and the other game threads to cores 1-2 at the first Swap. | Hung the first Swap in Ryujinx in 4 of 12 runs (re-masking running threads). Port 103 (dishonored-nx's, sa2-nx's) applies the layout from each thread at its own next wait instead, the cause of those hangs: 3 of 3 Ryujinx runs passed the first Swap since; off until a console trial. | Faster in sa2-nx: render thread 12.5 -> 11.5 ms, game thread 17.6 -> 17.3 ms |
| `banjo_renut_deferred_chain` | Begin*Vertices draws join the snapshot delta chain instead of a whole device copy. | The chain is one for every device; a game drawing on two devices from two threads gets a delta on the wrong device (emulator hangs in 4 of 8 runs). | Less copying; not measured since |
| `banjo_deko3d_eager_kicks`, `banjo_deko3d_latency_trim` | Input-latency A/Bs: the frame sent to the GPU in more, earlier parts; the render thread waits for the GPU to trim its queue. | Latency trials, per game. | Lower input latency is the aim; frame time may rise |
| `banjo_deko3d_skin_once`, `banjo_deko3d_light_branch`, `banjo_deko3d_gpr32` | GPU A/Bs of Banjo's fur and lit shaders: skinning once a frame, per-light branches, a 32-register cap. | The same pixels; whether faster is the console's to say (their configs/*_on.toml). Light branch and gpr32 act only on the pixel shaders Banjo's lists name (tools/lightbranch_shaders.txt, tools/gpr32_shaders.txt): in another game they change no shader, so they are no A/B targets there (vivapinata-nx, 2026-11-12: its in-run A/B halves of them ran at 2 fps, every pipeline looked up again as the variant key changed). | Not measured on the console |
| `banjo_gpu_zcull_greater` | Asks the driver for ZCULL in the GREATER direction (reversed depth). | A console trial; the picture should be the same. It acts only on the Mesa/NVK path (setenv NVK_ZCULL for banjo-nx's patched NVK); on deko3d it does nothing, and nothing is needed there: dkCmdBufClearDepthStencil writes SET_ZCULL_DIR_FORMAT itself (method 0x36F), GREATER when the depth clear value is under 0.5 and LESS otherwise, so a reversed-depth game that clears depth to 0 already culls in its direction (vivapinata-nx, from libdeko3d.a's disassembly, 2026-11-12). | Not measured |

## 5. tw2de_nx's fixes and accuracy options, off at the user's word

Ports 143-147 (2026-11-16) took these from tw2de_nx (recomp-nx's The Witcher 2 port, which grew from banjo-nx through
the FINAL FANTASY XIII ports). The user: "Do 1, 2, 3 make sure its off by default though", then "can you port the speed
ups as on by default": its speed-ups are on (docs/ultimate-fixes.md, rows 143-147), its fixes and accuracy options
below are off until a game turns one on and checks itself. Most are exact (the 360's results, found by tw2de_nx with
Xenia's hardware-recorded instruction tests or in its games). A codegen key goes in the game's codegen toml; a CMake
switch needs a rebuild (and a fresh PGO profile).

| Switch | What it does | Why it is off | Speed |
|---|---|---|---|
| `kernel_u64_args_whole` (port 143) | An export's 64-bit integer argument read whole. | Exact; off at the user's word (the base passes only the low word: XamFormatDateString / XamFormatTimeString's FILETIME, XamNotifyCreateListener's mask, an XUID). | None |
| `xam_swap_disc_signal` (port 143) | XamSwapDisc sets the completion event the game waits on (every disc is the one game folder). | Exact; off at the user's word (FINAL FANTASY XIII waited for ever at "insert disc 2"). | None |
| `kernel_scatter_read_offset_zero` (port 143) | NtReadFileScatter at offset 0 reads at 0 (only -1 / -2 mean the file's position). | Exact; off at the user's word (from nx-mod/rexglue-sdk-nx). | None |
| `TW2DE_FNMADD_SIGN` (CMake, port 144) | fnmadd / fnmsub and their single forms with the PowerPC's sign of zero and NaN (tools/fnm_sign.py's REX_FNEG_FUSED, sdk/include/rex/ppc/fpu_fma.h). | Exact; off at the user's word (GCC folds -std::fma into FNMADD / FMSUB: +0 where the 360 gives -0, a NaN negated; Xenia's tests: 160 cases). | An instruction or two more a site (Katamari: 272 sites) |
| `TW2DE_VSHIFT_PER_BYTE` (CMake, port 144) | vsl / vsr with each byte's own shift count, as the hardware (rex/ppc/intrinsics.h). | Exact; off at the user's word (the ISA's uniform counts give the same either way; Xenia's tests: 453 of 475 vsl cases with mixed counts). | Five NEON instructions; the default vsl goes through memory |
| `vcmpbfp_cr6_exact` (codegen, port 144) | vcmpbfp.'s CR6 only "eq" (all within bounds). | Exact; off at the user's word (the mask form set "lt" when every element was out of bounds). | None |
| `vupkd3d_exact` (codegen, port 144) | vupkd3d128: NORMPACKED64 from the right doubleword with its biases; the most negative field a quiet NaN in NORMSHORT2 / NORMSHORT4 / NORMPACKED32's y and z. | Exact; off at the user's word (Xenia's x64 unpack, from nx-mod/rexglue-sdk-nx; NORMSHORT2's NEON form, tools/vupkd3d_neon.py, takes only the default form). | A compare a lane |
| `fpscr_moves_exact` (codegen, port 144) | mtfsf's field mask in the PowerPC's order, mffs's upper word 0xFFF80000, builders for mtfsfi / mtfsb0 / mtfsb1 / mcrfs. | Exact; off at the user's word (`mtfsf 1, f` did not set the rounding mode; Katamari uses only `mtfsf 255`, the same both ways). | None |
| `extra_instructions` (codegen, port 144) | Builders for ~43 instructions the generator traps on (sdk/src/codegen/builders/extra.cpp: vaddcuw ... vpkpx, lmw, ldbrx, stdbrx, rotld, mcrxr, mfvscr, mtvscr, the OE forms; isync, icbi, mtmsr, lvsl128 / lvsr128, the LRU-hint vector loads and stores). | Exact; off at the user's word: a game with none of them sees no change. | None |
| `vmx_360_estimates` (codegen, port 144) | vrefp, vrsqrtefp, vexptefp, vlogefp, fres and frsqrte from the hardware's tables, bit for bit (rex/ppc/vmx_est360.h: Xenia's console tables, xenia-canary pull request #1342; xenia-edge's for the square roots). | Exact to the console, but not the base's default (the exact division and square root, or port 72's NEON estimates); off at the user's word. | Slower than port 72's forms (a table look-up per lane) |
| `vmx_dot_360` (codegen, port 144) | vmsum3fp128 / vmsum4fp128 with the console's rounding (float64 sums truncated to single). | Exact to the console; off at the user's word. With it, `vmx_360_estimates` or `exact_fp` on, the hand-written natives (app/src/cpu/hot/hand.txt) still compute the default forms: the guard finds their copies (and the copies that call them) one unit off and turns them off (Katamari: 19 copies, 2026-11-16), so a game that turns these on brings its hand natives along. | Slower (a scalar double sum) |
| `exact_fp`, `exact_fp_functions` (codegen, port 144) | The FPU and vector float results the 360 gives where the host's differ (rex/ppc/fpu_exact.h: the first NaN operand in the 360's order, QNaN for an invalid operation, the single-precision instructions with a double denormal operand); everywhere, or in the listed functions. | Exact; off at the user's word (a game's code with valid inputs never sees a difference). | A test of each result |
| `exact_fp_log`, `fp_origin_log` (codegen, port 144; diagnostics) | Every instruction exact_fp would change reported once with its function (the line for exact_fp_functions); every float instruction that makes a NaN or an infinity of finite operands reported with the guest's callers (sdk/src/core/ppc_exact_log.cpp). | Diagnostics (Katamari's PC run, 2026-11-16: 35 instructions and 18 origins reported, among them an fdivs 1/0 at 0x82282D80 whose fmuls then gave x86's negative default NaN, FFF8..., where the 360 gives 7FF8...). | A test of each result |
| `banjo_renut_fetch_filters` (port 145) | A texture is sampled with the filters its shader's fetch instructions give (mag / min / mip, anisotropy) where they give one, not only the fetch constant's (render/fetch_filters.cpp, read from the microcode when a shader is made). | Exact; off at the user's word (FINAL FANTASY XIII's light shafts: a linear blur of a point-sampled mask came out stepped); the draws whose shaders give them take the full draw path. | Katamari's shaders give none |
| `crt_xmemcpy_as_memmove` (port 146) | The native XMemCpy (which a game maps its own block copy to) is a memmove. | Exact; off at the user's word (a game's forward block copy may be called with overlapping ranges, which memcpy leaves undefined; from nx-mod/rexglue-sdk-nx). | None |
| `xconfig_host_time_zone` (port 147) | XConfig's TIME_ZONE_BIAS is the console's time zone (its offset now, DST included), so a game's local time is the player's. | Off at the user's word (the base answered 0: every game showed UTC). | None |

## 6. fnxf_xiii's and rehatched-nx's fixes and accuracy options (ports 158-176 and 179), off by the same rule

The user's rule for ports from other games (section 5): fixes and accuracy options off, speed-ups on. A game that needs
one turns it on in its toml.

| Key | Port | What it does when on |
|---|---|---|
| `banjo_xtimer_clamp_past` | 159 | an absolute timer due time in the past fires at once (a due time of 0 overflowed) |
| `banjo_xam_profile_all_types` | 159 | XamUserWriteProfileSettings keeps the int, float, date and string settings too |
| `banjo_switch_close_shutdown` | 159 | closing from HOME runs the app's shutdown (at most 3 s) first |
| `banjo_mnk_pad_keystrokes` | 159 | PC: the keyboard as a pad for XInputGetKeystroke too |
| `banjo_deko3d_vertex_texture_barrier` | 161 | a draw whose vertex shader samples textures waits for the GPU's earlier writes |
| `banjo_deko3d_uam_pressure` | 163 | uam's register pressure passes (another DKSH cache file) |
| `banjo_deko3d_sample_mask` | 166 | the game's MSAA sample mask for each draw |
| `banjo_wait_absolute_timeouts` | 166 | a guest wait with an absolute timeout waits until that time (it timed out at once) |
| `banjo_renut_scaler_crop` | 169 | the scaler's source rect picks the part of the front buffer shown |
| `banjo_fence_waits_exports` | 170 | a D3D fence completes once pending memexport results are in guest memory (a GPU sync) |
| `banjo_vblank_from_display` | 172 | the guest's vblanks follow the display's refreshes, not a 60 Hz timer |
| `banjo_renut_all_surface_headers` | 174 | every bound surface's header kept as bound (with chain reuse and the scan-out hand-over off) |
| `banjo_renut_edram_carry` | 179 | a colour target first drawn in a frame without a clear starts from the picture last written over its EDRAM base (scaled; idle while blur_renut_edram_inherit is on) |

`banjo_deko3d_swap_interval` (port 165) is a setting, 1 as before.

## 7. Keys on in the base that a port turned off for its game (reported, not confirmed here)

Each is a port's own finding on its own game. None comes with a comparison against a reference that shows what breaks,
so the base's default stays; a port that sees the same turns the key off in its toml after its own A/B.

| Key (default here) | The port and what it says | Its evidence |
|---|---|---|
| `banjo_late_input` (on) | S4ep1-nx (2026-11-15): Sonic 4 reads its controller on a frame thread of its own, not on the thread that swaps; the delay held that thread back and the game ran at a quarter speed on Renut. The key assumes input is read on the thread that swaps: check where a new game reads it | 152-284 steps in 10 s against 845-889 (its slow-motion check). A behaviour fault, not a picture one: the default is the user's call |
| `banjo_deko3d_native_16bit` (on) | deadpool-nx (cc060d4a): off "to prevent black 16-bit texture corruption on Switch" (Deadpool, Unreal Engine 3) | None given |
| `blur_renut_texture_pool` (on) | deadpool-nx: off "to avoid stale render target reuse" | None given |
| `banjo_renut_dynamic_texture_rows`, `banjo_renut_stream_redeem` (on) | deadpool-nx: off "for UE3 streaming safety", "for UE3 vertex safety" | None given |
| `banjo_async_present` (on) | deadpool-nx: off "to prevent 60-130ms frame-pacing latency spikes" | The numbers only |
| `blur_small_alloc_mb` (32) | deadpool-nx: 0 (Unreal Engine 3's own allocator) | None given |
| `spin_hint_yield` (codegen, on) | deadpool-nx: off "for UE3 spinlock efficiency" | None given |
| `banjo_renut_keep_unused_targets` (on) | deadpool-nx (ea2dffc5): off | None given |
| `banjo_renut_const_nan_flush`, `banjo_renut_vertex_finite` (on) | banjo-nx (17984e41, 2026-11-20; the user's): off for N&B, "off what N&B does not need and 1.0.2 did not have" | renut-nx 1.0.2 drew N&B right without either |
| `banjo_renut_stream_redeem` (on) | banjo-nx (2026-11-20): off for N&B | 0-1 streams made cacheable again in 12 min of the game (render-thread-turn-offs.md's rule) |
| `dishonored_renut_vertex_readable_check` (on) | banjo-nx (2b6ff1dc, 2026-11-20): off for N&B (the user's) | None given |
| `sasrt_gpu_vs_input_mask` (on) | banjo-nx (2026-11-21, port 251's handoff): off for N&B | Two in-run A/Bs of 122 in Showdown Town (console c28, c34): on, render thread +0.6 % and +0.8 % a frame, fps -1.1 % and -0.8 %, GPU +0.3 % |

deadpool-nx also turned on `blur_renut_edram_inherit` (off here, port 86), `audio_xma_output_done_when_drained` and
`audio_xma_kick_wake_idle_only` (off here) for Deadpool.

## Keeping this page

When a port reports that a key breaks its picture (with a comparison against the reference), its default goes to false
under the rule above, it gets a row in section 1 and in docs/ultimate-fixes.md, and the port that needs it is told to
set it in its own toml. A switch measured slower on the console goes in 3a with its numbers. A fix or accuracy option
taken from another port and left off at the user's word goes in section 5, with what it fixes and in which game.
