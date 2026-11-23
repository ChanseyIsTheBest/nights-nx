# NiGHTS into Dreams on the ultimate base: notes

What the rebase of nights-nx (1.0.2 and the media work of 2026-11-22) on the ultimate base found and decided, with its
evidence. The inventory is [ultimate-rebase-plan.md](ultimate-rebase-plan.md); the not-exact forms and their censuses
are [nights-near-exact.md](nights-near-exact.md); the base commit taken is [ultimate-sync.md](ultimate-sync.md).
NiGHTS's own records from 1.0.0 to 1.0.2 are [nights-notes.md](nights-notes.md) and its progress log.

## Decisions

| What | Decision | Why |
|---|---|---|
| Where | the branch `ultimate` in the worktree `D:\nights\nights-nx-ult`, from `master` (65ec444e); the base fetched as `refs/ultbase/ultimate` | the base's rebasing prompt; `master` is not changed |
| The project's internal name | `renut`, as the base and 1.0.2 (generated `renut_recomp.N.cpp`, the `renut` targets) | the same in both |
| The player's files | `switch/nights-nx/nights-nx.nro`, `renut.toml` beside it (1.0.2's names); NACP version 1.1.0 (the first release on the base, 2026-11-23; `1.0.2-ult` before) | a 1.0.2 install keeps working |
| The game files | `app/assets`: a directory junction to `D:\nights\nights-nx\app\assets` (default.xex and the title update's default.xexp) | the prompt: a junction, never a copy |
| The code generator | the base's own build (`D:\renut-ultimate\base\sdk\out\win-amd64\rexglue.exe`, 2026-11-21, built from the same sdk/src/codegen as this tree), run by hand (`tools/codegen.ps1`'s steps: REXGLUE_DUMP_IMAGE, `rexglue codegen renut_manifest.toml`) | D: had 8.8 GB free: no host SDK build of its own; PowerShell scripts are not run past the execution policy |
| The renderer | Renut only (the base's), Vulkan on the PC, deko3d on the Switch | as 1.0.2 |
| The tiling hooks | 1.0.2's form | 1.0.2's picture; the base's Katamari form uses the Flags / Z / stencil NiGHTS passes, a picture change to A/B first |

## Settings: a 1.0.2 player's renut.toml

The player's file is 1.0.2's (`app/nights_switch.toml`, written next to the NRO when missing; renut.toml as before), so a
player who has one keeps it. The compiled-in settings (`app/nights_switch_defaults.toml`) are 1.0.2's under the base's
names. `tools/nights_effective_keys.py` compares the two builds' effective settings key by key (each key's default as
written in its REXCVAR_DEFINE, then the compiled-in and player tomls), 2026-11-22: 1,049 keys in 1.0.2, 1,441 here,
1,038 in both.

| 1.0.2 key | Here | What the rebased build does with the line |
|---|---|---|
| `language` (auto, english, ...) | the SDK's `language` (blur-nx's; 1.0.2's src/nights_language.cpp is left behind) | reads it; "auto" is not one of its names and falls to System, the console's language, what 1.0.2's auto did |
| `banjo_renut_smaa`, `nights_true60`, `input_xbox_layout`, `input_joycon_sideways`, `input_joycon_sideways_stick`, `[input_p1]`, `[input_p2]` | the same keys | reads them |
| `nights_renut_double_buffer`, `nights_renut_queue_drain` (compiled in, not in the player's file) | `pacman_renut_double_buffer`, `pacman_renut_queue_drain` (the same code: pacman-nx's forms of sa2-nx's keys) | the compiled-in settings name the base's keys; an old line is ignored |
| `nights_present_60*`, `nights_diag_camera_frames` (off in 1.0.2, not in its tomls) | left behind (ultimate-rebase-plan.md list d) | an old line is ignored |
| `input_script_stick_cycle*` (test runs only) | left behind | an old line is ignored |

Effective values that differ, of the 1,038 keys both builds define: **none that the build uses**. `banjo_pool_lock`
(1.0.2 true, here false) is read only by src/cpu/banjo_pool_lock.cpp, N&B's fix, which neither build compiles.
`input_rumble` (the base's port 59, the game's own vibration: on in the base) was off in 1.0.2: the compiled-in settings
set it false, and the player turns it on in the game menu or renut.toml (the user's request, section "The game menu
and rumble"). `banjo_renut_shader_get_function` is on in
NiGHTS's settings (port 196's GetFunction keeps the shaders' containers: NiGHTS reads its shaders back).

The 403 keys only this build defines are the base's ports' switches, at the base's defaults (its universal fixes and
speed-ups on, the rest off: docs/off-by-default.md).

## Code generation (2026-11-22)

`rexglue codegen renut_manifest.toml` (the base's generator) on NiGHTS's config: 30 s, 165 files written, no warning;
`out/renut_image.bin` (game data, not committed). Then `tools/post_codegen.py` in full (its log: `out/post_codegen.log`).

## The PC build (2026-11-22)

`tools/build_pc.bat` with `RENUT=ON` (`app/out/pc`). Fixes the first builds needed, each in its commit: the shader
translator's dxc-bin binaries (not tracked: an ignore rule; copied from nights-nx's checkout), the tiling hooks wholly
in 1.0.2's form (the base's Katamari-form lines had merged cleanly into their bodies), port 173's Bind wrapper left out
(NiGHTS's Bind hook already takes Flags), Katamari's memcpy / memset forwarders pointed at NiGHTS's natives. The build
regenerated the code itself on its first run (its own generator relinked) and ran post_codegen.py again.

Runs (`tools/nights_pc_run.py`; the keys 1.0.2's PC runs passed: `--banjo_renderer=renut --renut_gpu_backend=vulkan
--license_mask=1 --headless=true`, with `--banjo_renut_shader_get_function=true --input_rumble=false`; game files
`D:\nights\game`):

| Run | What it played | Result |
|---|---|---|
| boot1 (120 s, a new save) | warning, logos, title, main menu, the story's opening movie | no crash; the only errors the `cri.txt` probe's (as 1.0.2); 30.0 fps; every self-check 0 differences (flush-mode flow 28.7 M checks, folded loads 19.4 M, r1 mirror 46.6 M calls, stack forwards 172 k) |
| route_hd1, route_hd2 (a copy of the save in `D:\nights\userdata`) | title, Main Menu (Brand New Dreams), Spring Valley: Claris on the stage, the Ideya palace, chips and links, the Nightmaren, an Ideya capture, the hint screen, the Dreams select | the picture as 1.0.2's (the HUD, the scenery, the characters); 400-650 draws a frame in gameplay at 29-30 fps |

The route for test runs (presses counted from the first input poll): `30:START,36:A,42:START,48:A,52:A,56:A,60:A,
66:LSUP@18` (the title at about 40 s, the Main Menu at 45-48 s, Spring Valley's gameplay from about 70 s), then
`input_script_sticks`; Sega Saturn Dreams with `45:DOWN` before the A at 48. A START during gameplay pauses it.

## The game menu and rumble (2026-11-22, the user: "turn on the in game menu and add rumble as an option from it")

The base's game menu (blur-nx's, app/src/game_menu.*) is built into this port: hold A+B+X+Y+L+R (input_menu_combo; F8
on the PC) to open it, + (Start) to close it; the game sees no input while it is open. Its Settings tab gets NiGHTS's
rows through the base's game hook (src/nights_menu.cpp, SetGameMenuRows, banjo-nx-ult's form):

| Row | Key | NiGHTS's change |
|---|---|---|
| Rumble | `input_rumble` (the base's port 59: the game's own vibration) | removed 2026-11-23 (the game never asks for vibration: "Rumble" below); until then default Off and always written to renut.toml |
| Frame rate * | `nights_true60` | in the place of the base's `frame_cap` (NiGHTS paces at the game's own interval): 30 fps / 60 fps, read at the start; 60 fps the default since 2026-11-23 (below) |
| Docked resolution | `banjo_renut_render_height_docked` | default 1920x1080 (1.0.2's) |

The rest are the base's rows (language, handheld resolution, SMAA, MSAA, the menu's colour, font and opacity; on the
Switch also the menu buttons, the Xbox layout, the Joy-Con rows and four-core mode). The new texts are in the menu's
twelve languages. `nights_menu_check` (off) logs the rows built in every language without the UI; on the PC (2026-11-22,
menu_check2): 11 rows in each language, every text present, rumble now Off / default Off / always in the file.
Not checked: the menu opened on the console (the PC's scripted runs cannot press F8).

`nights_true60_button_codes` is gone (the user, 2026-11-22: "get rid of button codes at 60fps just hardcode it on when
60fps is turned on"): the button-sequence codes register whenever `nights_true60` is on (src/nights_frame_hooks.cpp),
and an old renut.toml's line for the key is ignored. 1.0.2's default was already on.

60 fps by default (the user, 2026-11-23: "I want the game to be 60fps by default 30fps as an option instead"):
`nights_true60` defaults to true (src/nights_frame_hooks.cpp), the player's renut.toml and every config of
dist/switch/nights-nx/configs/ say `true`, the game menu's Frame rate row defaults to 60 fps, and
configs/renut_60fps.toml became configs/renut_30fps.toml (`nights_true60 = false`). A renut.toml already written with
`nights_true60 = false` keeps 30 fps. `banjo_renut_shader_get_function` defaults to true as well (it was on in the
Switch's built-in settings only): at 60 fps NiGHTS reads its pixel shaders back through D3DPixelShader_GetFunction,
and with the key off the PC build wrote past the copy's destination (sub_82337880, at 0x49910070) at the title.

The one black screen at 60 fps on the console (2026-11-23, boot 2 of that build: the game thread at 100 % after the
first frame, the GPU idle, no frame counted) did not come back in five more boots at 30 and 60 fps; its cause is not
known. A stack-sampled 60 fps boot (switch_profile_stacks, out/switch_logs/after/) is the profile of the machine copies
below.

## MSAA (2026-11-22, the user: "make sure MSAA works")

The game menu's MSAA row is the base's `msaa_level` (Off / 2x / 4x), which only lowers the sample count a game asks for.
NiGHTS asks for none: every render target it makes is single-sample (the `[formats]` log lines now name the guest's
sample count and the host's: 1280x720 8_8_8_8 colour and D24S8 depth, 1 sample, in every run). So the row did nothing.
`renut_msaa_single_sample` (new, render/surfaces.cpp; off in the base, on in NiGHTS's compiled-in settings; sa2-nx
1.0.6's sa2_renut_msaa in the base's form) gives the game's single-sample targets the level's 2 or 4 samples on the host;
the game's resolves downsample them as a game's own MSAA targets'. The targets' key has the level, so the menu's change
makes them anew from the next frame. SMAA steps aside on a multisampled target (its pass takes single-sample ones only):
with MSAA on, MSAA smooths the 3D and SMAA does nothing. Default unchanged: `msaa_level` 1 (off) and SMAA on, as 1.0.2.

PC check (Spring Valley at the same moment, 90 s, SMAA off, three runs msaa_r1 / _r2 / _r4): the targets made with 1, 2
and 4 samples; no error but `cri.txt`'s, no validation message; 29-30 fps at 2x, 26.6-30 at 4x (the PC's GPU 0.2 -> 2.9 ms
a frame at 4x). The difference between the 1x and 4x captures lies on the polygons' outlines (Claris's silhouette, the
palace, the terrain's edges, the HUD's shapes) and nowhere else: MSAA. Alpha-tested edges (the sign, the trees' leaves)
are not smoothed by it, as on any MSAA. Not measured: the console's GPU cost at 2x / 4x.

## Machine copies of every profiled function (2026-11-23, the user: "i want all profiled functions to have a machine copy unless they are ineligible")

The base's method (docs/optimising-functions.md, "Every opportunity"; docs/hand-and-machine-optimising-guide.md,
section 2): the bulk list remade from the console profile with `--min-samples 1 --merge`.

- The profile: the stack-sampled 60 fps New Dreams boot of the rebased build 2f4c6409 (the user's console,
  out/switch_logs/after/rex/rex_profile.log, 2 minutes at 54-56 fps), read with that build's ELF (app/out/switch/renut).
- `python tools/switch_hot_bulk.py PROFILE ELF --min-samples 1 --min-instructions 0 --merge --write
  app/src/cpu/hot/localized_bulk.txt`: 74 functions added (98 -> 172), every generated function the profile sampled; one
  left out, a tiny leaf (12 instructions or fewer, no call: the generator inlines it already). None was refused (atomics,
  setjmp, the global lock) or kept out by a source's mention.
- `--min-instructions 0`: the base's rule against copies under 40 instructions (port 78, sasrt-nx's idea 30) is for
  hooked copies, whose callers can no longer inline them. NiGHTS's Switch NROs have the copies in place (port 81), where a
  small copy stays inlinable, so the small ones are in too.
- The two older console profiles on disk (D:\nights\logs\rex and D:\nights\newlogs and videos\logs\rex, 2026-10-29) are
  not used: their builds' ELFs are gone (their code spans, 0x1B53000 and 0x1B54000, match no ELF on disk), and a profile
  read with another build's ELF names the wrong functions.
- `REXSDK_DIR=sdk python tools/post_codegen.py` (out/post_codegen_mc.log): 172 bulk copies in 12 shards, none refused.

The PC check (verify_mc_nd60, 300 s, New Dreams at 60 fps into Spring Valley, the HD route without `45:DOWN`, the
Saturn-unlocked save; `banjo_cpu_verify`, guard 4000 / 1): 130 of the 172 bulk copies reached (57 of the old 98 in the
same route before, so about 73 of the 74 new ones), 91.1 million calls compared, 0 differences (7 differed once and not
when repeated: another thread's inputs); r1 mirror 232.7 million calls and 194,855 stack forwards, 0 differences.

The instrumented PGO build comes after these copies (a function changed after the instrumented build keeps no profile):
app/out/switch-pgo-gen, `-DBANJO_PGO=generate -DBANJO_SWITCH_LTO=ON`, in place (its profile goes to pgo-inplace/).

## Rumble (2026-11-23, the user: rumble "doesn't seem to trigger")

The setting reaches the pad: `input_rumble` (the base's port 59) is read live by InputSystem::SetState, and the Switch
driver logs "input: rumble ready on npad ..." at the first vibration call that reaches a pad, even a zero one. No console
log has that line, including the 2-minute 60 fps New Dreams boot with `input_rumble = true`.

The game does not ask. NiGHTS's XInputSetState wrapper is sub_8250CDC8, called only by the pad class's two methods
(vtable 0x820F0850, made by sub_82596068): slot 16 sub_82595860 (set the motors: each value doubled, clamped to 65535)
and slot 17 sub_82595800 (stop, only when set). With `input_diag_users` the PC now logs the game's XamInputSetState
calls every 10 s too (sdk/src/kernel/xam/xam_input.cpp). rumble_nd60 (240 s, New Dreams at 60 fps into Spring Valley):
XamInputGetState on user 0 about 590 times every 10 s, connected; XamInputSetState not called once. No game file names
vibration or rumble (ASCII or UTF-16 either way round), so there is no vibration option in the game's menus. The scenes
not played (bosses, Elliot and Claris on foot, a time-out) are not checked: with `input_rumble = true`, a console log
with "rumble ready" would show the game asking there.

Rumble taken out of the settings (the user, 2026-11-23: "remove rumble from the settings please since the game doesn't
use it"): the game menu's Rumble row is removed (src/nights_menu.cpp), the player's renut.toml no longer lists
`input_rumble` (app/nights_switch.toml, dist/switch/nights-nx/renut.toml, tools/make_nights_tomls.py), and the README
table drops it. `input_rumble = false` stays in the compiled-in settings, so a renut.toml written before keeps working.
