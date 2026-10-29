# NiGHTS into Dreams... port: notes

This file records the port: what each step found, with its evidence. The project is a copy of banjo-nx's working tree
of 2026-10-28 (its HEAD `1889dab6` plus the 41 files it had modified, the 10-28 renderer rounds), taken as the base as
the user asked. Comments that say "banjo-nx", "peggle2-nx", "zumasrevenge-nx" record findings from those ports.

Credits: reNut's renderer (masterspike52; reuse in the user's ports permitted), sa2-nx (input, CPU rounds),
NetherSX2_nx (four-core layout, MIT), battd_nx (the GPU arena), bejeweled3-nx and the zuma ports (the hot-function
method, the Direct3D-layout tools). GPL-3.0 where the sources are. Game data is never committed or distributed.

## The game (2026-10-28)

The user's files are in `D:\nights`:

- `NIGHTS into dreams.rar`: one package, `NIGHTS into dreams/58411268/000D0000/ACF6333A...`, an XBLA game (content type
  `0x000D0000`), title **58411268**, a read-only LIVE STFS volume. `tools/stfs_extract.py` (from peggle2-nx) read its
  524,144 blocks: 3,472 entries, 2,041 MiB, into `D:\nights\game` (outside the repository).
- `NiGHTS into Dreams... (World) (v1) (77D4B0B3) (Title Update).zip`: `Cache/TU_1C424J8_...`, a LIVE package of content
  type `0x000B0000` (title update) holding `default.xexp` (509,952 bytes), extracted to `D:\nights\tu`.
- **`default.xex`** (`tools/xex_info.py`): original PE name `Nights_360.exe`, base `0x82000000`, entry `0x82336DB0`,
  image size `0x26E0000`. Imports `xam.xex` (82) and `xboxkrnl.exe` (302), 2.0.21173. Static libraries: XDK
  **2.0.21173** (XAPILIB, D3D9, D3DX9, XGRAPHC, XAUDIO2, XAPOBA, XMCORE, XMP, XNET, XONLINE), MSVC 16.0.11886. 21173
  lies between Zuma's Revenge's 20871 and Peggle 2's 21256, which share one Direct3D device layout (the Peggle 2
  session's message of 2026-10-28; zumasrevenge-notes.md "The device layout").
- **The title update is applied**: the SDK's loader looks for `<xex>p` beside the xex (sdk/src/system/user_module.cpp)
  and the code generator loads through it, so `app/assets/default.xexp` next to `default.xex` makes both the codegen
  and the run use the patched image: "XEX patch applied successfully: base version 0.0.0.1, new version 0.0.1.1".
- **Sections** (PE header of the loaded image): `.rdata 82000400-8210E074`, `.pdata 8210E200-82128C90`,
  `.text 82130000-825F0748` (4.75 MB of code), `.data 82600000-842CA8F4` (29 MB), `.XBMOVIE`, `.idata 842D0000`,
  `.XBLD`, `.reloc`.
- **Data**: `afs/` (CRI AFS archives, ADX music, CSB cue sheets), `cse/`, `fs/`, `shader/`, the achievement icons,
  `ArcadeInfo.xml`.

## Code generation (2026-10-28)

`tools/codegen.ps1` (nights-nx's: `rexglue codegen renut_manifest.toml` with `REXGLUE_DUMP_IMAGE=out/renut_image.bin`)
runs in 15-20 s and writes 169 files (85 MB) into `app/generated`.

- **First run**: three "target not in any function", leaf functions reached only by a tail branch (no `.pdata` entry),
  declared with their sizes in `app/config/renut_funcs.toml`: `0x823B9BA0` (0x1C), `0x823B9070` (0x18), `0x823F5310`
  (0x8).
- **setjmp / longjmp** by shape, as in zumasrevenge-nx: setjmp `0x825872D0` (a hook word at `0x842CA7C0` tried first,
  `bnectr`, then `mflr`, `mfcr`, `stfd f14,0(r3)`: the only `stfd f14,0(r3)` in the image), longjmp `0x82586EA0` (the
  check of +312, `mr r7,r3`, `lfd f14,0(r7)`: the only one).
- **Pointer sweep** (`tools/pointer_fixpoint.py`, adapted to this layout): pass 1, 7,777 gaps, 114 candidates, 84
  accepted (28 rejected as tail branches into a function body, 2 as SEH scope labels); fixpoint in pass 2.
- **Jump tables** (`tools/switch_check.py --code 82130000:825F0748`): none truncated, wrong or missed. Its 14 "table
  jumps with no generated code" are generated (the checker does not read `switch (r29.u32)`, the register-local
  spelling): `sub_8219D330`'s table at `0x8219D3B0` has its 10 cases. Two "odd shapes" (`0x8257B928`, `0x8257CE80`) are
  generated as indirect calls: to look at if a run reaches them.
- **Register locals** (zumasrevenge-nx's audited set: CR / CTR / XER / reservation and the non-volatiles as locals,
  `skip_msr`, the float-mode diet; `non_argument_as_local` and `elide_lr_stores` left out until they have NiGHTS's
  audit). `tools/read_before_write.py` (taught that `rex::ppc::CompareAnyMode(crN, ...)` writes crN) reported 46:
  - five SEH funclets of the pointer sweep take their owner's r30 / r31 (`ctx.r1 = r31 + 112`): `share_registers`
    (`0x82184720`, `0x822EC664`, `0x822EC678`, `0x822EC68C`, `0x822EC6BC`);
  - v64-v72 read and never written by XDK library code (`sub_824DB8B8`, `sub_824DBEF0`, `sub_825C1A00`..`sub_825C22D0`:
    v69 / v72; `sub_825C33F8`: v64 / v65): `non_volatile_as_local_keep = ["v64-v72"]`, Zuma's Revenge's range;
  - harmless, as banjo-nx classified the same shapes: the MSVC EH header words (`0x8259A7C0`, FuncInfo) in front of
    22 functions (dead loads); setjmp's callers' `env.rN = rN` copies (6) and setjmp itself; `xor r30,r30,r30` (2);
    `subfe` of one register (1); software-pipelined vector loops in functions that save their own registers
    (`sub_824DD3E0`, `sub_824DDD80`, `sub_823851E8`: values carried round the loop, first read on the entry path).
  After the marks: 34 reports, all of the harmless kinds.
- banjo-nx's per-function lists were N&B's and are empty here until NiGHTS has its own census or profile:
  `app/indirect_promotions.txt`, `app/config/{hot_edges,hot_functions,locals_optout,switch_goto}.txt`,
  `app/config/{lr_write_same_choice,r1_local_choice}.json`, `app/src/cpu/hot/{localized,localized_bulk,hand}.txt`
  (banjo's hand list, memcpy / memset / Direct3D / XAudio at N&B's addresses, is kept in `out/banjo_hand.txt`).
- Hooks in the app at N&B addresses: of the 394 `sub_X` names the app's sources define, 3 are NiGHTS function starts
  (`sub_82222EA0`, `sub_822899D0` in Renut's state hooks, which get remapped; `sub_82277C30` in an unbuilt file). The
  rest name no NiGHTS function and are dead code until remapped.

## Renut's Direct3D layer at NiGHTS's addresses (2026-10-28)

Renut's hooks in banjo-nx name N&B's addresses (XDK 2.0.7645). NiGHTS's XDK 2.0.21173 sits between Zuma's Revenge's
20871 and Peggle 2's 21256, the two ports that already carry Renut's layer for that era, so the hooks move
N&B -> Peggle 2 -> NiGHTS (and N&B -> Zuma's Revenge -> NiGHTS for what Peggle 2 does not link).

- **Pairing** (`tools/d3d_propagate_p2.py`): seeds are functions whose shape (opcodes and registers, no immediates) is
  unique and equal in both images, plus the render / sampler state tables ({getter, setter, default} rows of 12 bytes:
  Peggle 2's `0x828DFDB8` / `0x828E0278` and NiGHTS's **`0x82600380` / `0x82600840`**, the only places whose default
  columns equal Peggle 2's word for word); then each aligned `bl` of a paired function pairs its callees, to a fixpoint.
  A sequence alignment of the two libraries paired little (`tools/d3d_align_p2.py`): the link order differs.
  Peggle 2 -> NiGHTS: 2,360 pairs (1,632 unique shapes, 505 call sites, 224 state functions), 2 conflicts dropped;
  Zuma's Revenge -> NiGHTS: 3,564; N&B -> NiGHTS directly: 561.
- **Which hooks**: each port's own tree is the truth (hand fixes made after its JSON map, e.g. Peggle 2's Resolve hook
  on the public wrapper), read from the `// N&B sub_X` notes of its hook lines. Of banjo's 363 hook / callee addresses:
  132 found through both Peggle 2 and Zuma's Revenge and **all 132 agree**; 186 through Peggle 2 only; 6 through Zuma's
  Revenge only (DrawVertices `0x823519F0`, Present `0x82346CC8`, InsertCallback `0x82340658`, Acquire / Release
  ThreadOwnership `0x82345170` / `0x823451B0`, GetDisplayMode `0x82342D20`); 10 by N&B's unique shapes (the Lock /
  UnlockRect / LockBox / GetDesc / Get3DSurfaceDesc functions, GetViewport `0x8233D8B0`); 9 Peggle 2 hooks the
  pairing missed were paired by hand and checked word for word (VB / CB Unlock, GetLevelCount, the two thunks, the four
  Set{Vertex,Pixel}ShaderConstant{B,I}: `0x82350758` / `0x823507B8` / `0x82350818` / `0x82350870`). 28 are not linked
  by NiGHTS (tiling, command buffers, tessellated draws, SetClipPlane, SetVertexFetchConstant, ...) or are N&B game
  hooks; they stay in `#if 0`. Result: `out/renut_map/nb_to_nights_final.json`, applied by
  `tools/renut_hook_remap_nights.py`.
- **Device layout = Peggle 2's**: on 559 Direct3D function pairs of identical shape, 11,651 load / store displacements
  below 0x7000 are equal; the 232 that differ have no common delta (low halves of global addresses, and the XDK build
  number itself, 21256 -> 21173). So peggle2-nx's device-offset sites (`out/renut_port/device_offset_sites.json`,
  `tools/renut_device_offsets.py`) apply as they are: all 152 changed sites found in today's tree. banjo-nx's code
  since 10-19 had 19 more raw N&B offsets (draw.h's offset -> field table, the delta range in queued.cpp, the gamma
  field, a stream-source touch range); rewritten by hand to the 21256 layout.
- **Guest data addresses** (constant blocks found by content, variables by the same instruction of the paired
  function):

  | What | N&B | Peggle 2 | NiGHTS | How |
  |---|---|---|---|---|
  | render / sampler state tables | `0x82E506F0` / `0x82E50BD0` | `0x828DFDB8` / `0x828E0278` | `0x82600380` / `0x82600840` | defaults equal |
  | format table | `0x820052F8` | `0x8207A590` | `0x82001338` | 128 bytes equal |
  | caps (D3DCAPS9) | `0x82183B00` | `0x8207AA80` | `0x82001548` | 304 bytes equal |
  | full viewport | `0x820018DC` | `0x8207A610` | `0x8200129C` | 24 bytes equal |
  | build qword | `0x82F39B38` | `0x828E0370` | `0x82600930` | `0x0002000052B54000` (21173) |
  | VdGlobalDevice import slot | `0x820007FC` | `0x820008E0` | `0x82000598` | holds `0xFFCAE000` (`0x82000594`: `0xFFCAD000`) |
  | export switches (512 px / NaN vertex) | `0x82FAD208` / `0x82FAD204` | `0x82D75094` / `0x82D75090` | `0x82798DA4` / `0x82798DA0` | same instruction |
  | HiZ owner | `0x82FA36C8` | `0x82D7509C` | `0x82799DD0` | 3 uses agree |
  | no-large-pages flag | `0x82F39E34` | `0x82906EB4` | `0x827E94E4` | 2 uses agree (XPhysicalAlloc) |
  | GetCurrentThreadId / XMemFree | `sub_822402A8` / `sub_822399D8` | `sub_8260BF88` / `sub_823189E8` | `sub_8250C150` / `sub_8250C120` | paired |

## First boots on the PC (2026-10-29, the SDK's emulated GPU)

- Build: `tools/pc_build_bg.bat <log>` (tools/build_pc.bat with `EXTRA_CMAKE=-DBANJO_RENUT_RENDERER=OFF`), folder
  `app/out/pc-nx`; banjo-nx's N&B-only sources are not built (`NIGHTS_NB_SOURCES`, CMakeLists.txt), 126 of their
  settings are stubbed (`src/nights_dropped_cvars.cpp`). Needed from banjo-nx's `out/`: the host glslangValidator and
  the deko3d spirv-cross / uam builds (copied to `out/`).
- Paths: `app/out/pc-nx/renut.cfg` (game `D:\nights\game`, user data `D:\nights\userdata`); without it the PC build
  waits in the path wizard. `D:\nights\game\default.xexp` (the TU) sits beside `default.xex` for the run too.
- `boot1` (100 s, `--gpu_plugin=xenos`, `input_script` START / A presses every 4 s): no crash, no unimplemented call;
  the CRI file system's `cri.txt` probe fails as on a console without it (harmless). At 64 s the game loads
  `stage_1_sdf.bin` and streams `alp_1a.adx`; the 70 s capture is Claris in Spring Valley with the HUD
  (`capturas/captura_007_0070s.png`). **The PC boots to gameplay with no game-specific fix.**

## Renut on the PC (2026-10-29, Vulkan)

Fixes found by running, each in the code with a `nights-nx` comment:

| Symptom | Cause | Fix |
|---|---|---|
| banjo-nx's tree does not build Renut for the PC | its 10-27/28 rounds put shared helpers inside the deko3d-only block, defined `RepeatCensusReport` twice, used `far` (a Windows macro) | helpers out of the block, the recording census renamed `RecordingRepeatCensusReport`, a variable renamed |
| static asserts on the snapshot | XDK 21256 / 21173's snapshot: 40 delta chunks (N&B 39), last chunk 32 bytes (N&B 40) | asserts and `TakeLastChunk` (a 32-byte NEON form) for this layout |
| guest write to unmapped `0x49910070` right after CreateDevice | NiGHTS asks for the **automatic front buffer** (N&B does not): its InitializePresentationParameters `sub_8234F810` checks +60, calls CreateTexture `sub_8234D218` (w, h, 1, 1, 0, front format, 0, 3) and stores it at +15232 | made the same way (zumasrevenge-nx / peggle1-nx's fix), kept at `AutoFrontBuffer` |
| the same write again, from `sub_82351100` | **D3DPixelShader_GetFunction** (`sub_82351100`) / **D3DVertexShader_GetFunction** (`sub_82351158`) copy from the shader object's container fields, which Renut's records do not have | bejeweled3-nx's fix: the record keeps its container, the two functions hooked (word for word Zuma's Revenge's `sub_823142B0` / `sub_82314308`) |
| tiling | NiGHTS links **BeginTiling** `sub_8234AB48` / **EndTiling** `sub_8234AEB8` (Peggle 2 and Zuma's Revenge do not) | hooked; the private tiling words re-derived on the aligned functions: N&B 0x31B0 / 0x31B4 / 0x3384 / 0x3390 / 0x33A0 / 0x33A4 -> 0x3328 / 0x332C / 0x34FC / 0x3500 / 0x3510 / 0x3514 (each seen in NiGHTS's BeginTiling: `stw r8,13096`, `stw r10,13100`, `stw r25,13564`, `std r27,13568` x2, `stfs f31,13584`, `stw r26,13588`) |
| 95-132 game frames a second; the intro FMV and the presses out of step | Renut presents without vblanks; NiGHTS presents through **D3DDevice_Present** (`sub_82346CC8`), not Swap, with presentation interval 2 (device+14000, the PresentInterval state's word too) | `src/nights_frame_hooks.cpp` wraps Swap and Present (Renut's sides shared, `BANJO_RENUT_HOOK_SHARED`) and paces each present at the game's interval (peggle2-nx's PaceRenutSwap; DEFAULT 0 = one vblank, IMMEDIATE = none): **30.0 frames a second** |
| the game is the trial ("Unlock Full Game", "Night Over" after a time) | XBLA licence | `license_mask = 1` (as the other XBLA ports) |
| full game stops at the title after START | it asks "No Game Data was detected ... Create Game Data / Select a storage device / Continue Without Saving" (XamShowMessageBoxUI), an ImGui dialog nobody clicks in a test | `headless = true`: the SDK takes the focused button, **Create Game Data**; `NightsGameData` and `NightsAngelData` are created in the user data folder |

- `renut8` (130 s, full game, headless): warning, Sonic Team, title, main menu, Spring Valley's intro, Claris in the
  dream (random sticks from 75 s), the stage's "Night Over" when its time ran out; 29.8-30.0 frames a second;
  render thread 0.2-1.4 ms and GPU 0.5-1.3 ms a frame. No crash, no draw dropped, no shader missing.
- **Census (porting-guide-renderer section 2), first readings** (PC, title and Spring Valley): (1) draws are all
  immediate calls, 0 replayed from recorded command buffers ("draw sources: 6825 immediate"); NiGHTS links no
  command-buffer API (BeginCommandBuffer has no counterpart). (2) 0 % of draws on the fast path (`fast draws: 0
  fast, 6825 full`): banjo-nx's fast path is built around recorded buffers; to look at for the console. 600-9,600
  draws a frame in this first look (title ~25, gameplay ~250-300).

## The Switch build in the emulator (2026-10-29)

- emu4 (Ryujinx, this port's own data folder, `dist/emu_base.toml`: captures every 15 s, every verify key on):
  warning, SEGA, Sonic Team, title, attract loop, menus, Spring Valley gameplay, Dreams select. Verify: 0 differences
  on every check; snapshot deltas 31,000 draws a 10 s window checked, 0 differ. deko3d fast path 100 % of draws (it is
  deko3d-only: the PC's 0 % above is the Vulkan path). About 100 draws a frame in gameplay, 29.6 fps, render thread
  6.8-7.8 ms a frame.
- **Not a difference: the hollow squares over the SEGA logo's light streaks.** They are part of the movie
  (`afs/SEGA_HD.wmv`, the WMV decoder on the CPU): a PC capture of the same moment (`runs/sega2`, one capture a second)
  shows them too; the emulator decodes the movie at 6.6-8.7 fps and so catches frames with more of them. Before that
  was clear, ruled out by reading: the deko3d sampler modes, the primitive mapping, the AArch64 forms of every VMX
  helper the decoder uses, and the game language (English on both).

## Hot functions (2026-10-29, step 5)

From the emu4 ARM profile (`out/emu/emu4/rex_profile.log`; main thread: sub_822DD9F8 23 %, memcpy 22 %, the
RtlLeaveCriticalSection funclet sub_8233A590 6 %, sub_822D9CB0 4 %, sub_822D9DD8 2 %; second game thread:
sub_823EAFD8 26 %).

**Hand-written natives** (`app/src/cpu/hot/banjo_hand_natives.h`, `hand.txt`, each in front of its generated copy,
each with its key and the hook's guard):

| Function | What | Key |
|---|---|---|
| sub_824FBC50 | C runtime memcpy (banjo's native; the same instructions as N&B's sub_82BB4510, tools/crt_signatures.py) | `banjo_hand_memcpy` |
| sub_824FBB50 | C runtime memset (banjo's; N&B's sub_82BB4A40) | `banjo_hand_memset` |
| sub_822D9CB0 | point times the matrix stack's top, divided by w (vrefp + two Newton-Raphson steps) | `banjo_hand_transforms` |
| sub_822D9DD8 | direction times the top's 3x3 | `banjo_hand_transforms` |
| sub_823EAFD8 | resets up to 16 float buffers (first float, then another for the rest), NEON stores | `banjo_hand_buffer_reset` |

The transforms compute lane by lane with the generated code's separate multiplies and adds in the same order and
operand order (`-ffp-contract=off`, flush mode as the generated code sets it); the buffer reset converts its two
floats with the generated code's SingleToDoubleAnyMode / DoubleToSingleAnyMode. Each declines what it does not cover
(the early returns, overlaps with the stack slot or the block, addresses from 0xE0000000) and sets every integer
register the original leaves (hand.txt `exact=`); only the volatile vector registers are scratch. sub_822DD9F8 (1,302
instructions) is a localized copy (first in the bulk list), not hand-written; sub_8233A590 is a funclet whose samples
are the critical section's leave (the SDK's), nothing to hand-write.

**Bulk copies**: `localized_bulk.txt` = the top 100 of the ARM profile (at least 2 samples, tiny leaves and refused
forms left out; 99 after the buffer reset became a hand native), 12 shards.

**inline_memcalls** (post_codegen) had N&B's memcpy/memset addresses as defaults and inlined nothing here: now NiGHTS's,
376 memcpy and 166 memset call sites of a constant 1-64 bytes inline.

**Checked** (PC, `runs/hot1.log`, 140 s through the menus into Spring Valley, guard on the first 4,000 calls of each
thread then 1 in 16): 4.25 million calls compared, **0 differences** (memcpy 290,000, memset 66,000, point transform
146,000, direction 4,200, buffer reset 11,800; 93 of the 99 bulk copies reached).

## Codegen: elide_lr_stores and non_argument_as_local (2026-10-29, step 4)

Both from banjo-nx (`docs/codegen-enhancements.md` there), left off at the first codegen until their audits:

- `elide_lr_stores`: no `ctx.lr = <return address>` before a call. Nothing built here reads ctx.lr except the
  verify code (banjo_pool_lock and mullhwucrash, which do, are not built).
- `non_argument_as_local`: r0, r2, r11, r12, f0 and v32-v63 as C++ locals. `tools/non_argument_audit.py` on a fresh
  tree generated without it (`--ignore-stamp`), no-return functions longjmp `sub_82586EA0`, the C++ throw
  `sub_824FE6A8` and its two std:: wrappers `sub_82414D78` / `sub_82414DB0` (without those three, 78 false RET lines:
  the callers' reads after a throw). Findings: 3,450 MSVC unwind funclets (`addi r31,r12,-N`), the VMX save and
  restore helpers, and the WMV decoder's asm routines, which hand v32-v63 to each other (sub_825A65A8 / sub_825A68F0
  and their callers) and read some after an indirect call. The keep list (`app/config/renut_nonarg_keep.toml`, 3,573
  functions) is every ENTRY and RET function, the RET sources, and every function that writes any of v32-v63 at all
  (301: an indirect call has no hand-over). After generating with the option, `--with-option` reports 3 HANDOVER and
  their RET / ENTRY lines, all the generator's setjmp copies (`env.r11 = r11` at a setjmp, back after a longjmp) in
  functions that never read the copied value with the option off: the same pattern banjo-nx documents as harmless.
  12,764 of 17,774 functions localize the registers.

## PGO + LTO (2026-10-29, step 6; banjo-nx's docs/pgo-lto.md)

- Instrumented build `app/out/switch-pgo-gen` (`-DBANJO_PGO=generate`, from scratch with `-j 10`: 17 minutes; NRO 45 MB,
  159 objects in `gcov_info`).
- Two Ryujinx sessions with `dist/emu_pgo.toml` (renut_play.toml: profilers and diagnostics off; the copies called
  directly; a scripted route): pgo1 (380 s: the intro movies, title, menus; the instrumented NRO runs at 11-15 fps, so
  the script's presses landed early) and pgo2 (450 s, a slower script: about four minutes of Spring Valley, Night Over,
  the Dreams screen, Spring Valley again). Streams kept in `out/pgo_streams`, merged by `tools/pgo_merge.py`: 159
  `.gcda` files in `pgo/` (not in git), 2 runs, largest arc count 3.8e8.
- Optimised build `app/out/switch-pgo-use`: `-DBANJO_PGO=use -DBANJO_SWITCH_LTO=ON`. The 60 fps code went in after
  the profile (its few changed functions have no profile; `-fprofile-partial-training` treats them as without one).

## 60 frames a second (2026-10-29, the user's final goal; `nights_present_60`, off)

NiGHTS ties its game speed to its frame rate and presents at interval 2. Running the game faster is not the way (it
would run faster), so the screen gets an extra frame instead: `src/renut_engine/gpu/backend/frame_double.cpp`.

- The render thread logs a game frame's work as it runs it: every draw with the device snapshot it ran from (the
  rebuilt device of the snapshot deltas: whole), its Draw*UP data, every clear, resolve and tiling change.
- After the frame is presented it runs the log again into the same targets, ending with the Present's resolve into the
  front buffer, and presents that half a game frame later (a sleep to the measured period / 2). For each draw it finds
  the same draw in the frame before (its vertex and pixel shader, stream 0, index buffer, render target, first vertex
  fetch word and arguments; the n-th draw with the same key matches the n-th), and moves each vertex shader constant
  register half a frame on: c + (c - p) / 2. A register whose largest change is more than `nights_present_60_jump`
  (0.25) of its size keeps its value (a camera cut, a flag, a wrap); so does every register of an unmatched draw.
- The game, its timing and its input are untouched: the extra frame is a prediction from the last two frames (no
  added latency, unlike interpolating between two frames, which would hold the real frame back half a frame).
- What it cannot move: geometry the game writes every frame (particles, sprites through Draw*UP, the HUD's own
  vertices), pixel shader constants and textures (scrolling UVs), so those still step at 30. Overshoot where motion
  turns sharply (one half frame). The movies stay at their own rate.
- PC (`runs/p60a`): 300 of 300 game frames doubled, 601 presents in 10 s, the game at 30.02 fps; in gameplay 90 % of
  draws matched, 97 % of the changed registers moved (3 % kept as jumps). Render thread work doubles (the replay);
  GPU work doubles.
- Ships as `configs/renut_60fps.toml` for the user to judge (it changes the picture).
- Visual check (`runs/p60v`, `banjo_video_s` = 16 consecutive presented frames in Spring Valley, half size): the
  scene moves smoothly from frame to frame, no broken geometry; the differences between consecutive captures are
  even (20.6-21.8 mean per channel; the capture ran at 26.5 a second, so the pairs mix real and extra frames, and
  duplicated or misplaced extra frames would show as alternating small and large steps).

## Movies on the Switch (2026-10-29, emu7)

The intro movie, the SEGA and Sonic Team logos and the story cutscenes are WMV files (`afs/*.wmv`) played by the XDK's
own decoder (`xdk\del\codecs\wmv\codecdsp-cayenne`, the same library banjo-nx's FMVs use). In Ryujinx they play at
**2-10 frames a second** (emu7: four minutes of the attract intro): the decoder runs on one guest thread, busy about
70 % of the samples, spread over many functions (the deblocking filters sub_824A6820 / sub_824A63F0 12 % and 9 %, then
sub_825C33F8, sub_825C8000, sub_825C80C0, sub_8259FD10, sub_825C2F50, sub_825BF900 at 3-4 % each; all localized copies
now). Hand-written natives of a few of them cannot give the ~6x a 30 fps movie needs. What would: decoding the files
with a host decoder behind the game's calls into the XDK's movie player: the SDK's FFmpeg build already includes the
WMV3 / VC-1 decoder (`sdk/thirdparty/CMakeLists.txt`, a masseffect-nx addition, `config_masseffect_wmv3.h`; no caller
in this tree, so masseffect-nx's own movie code is the place to start). A project of its own (the player's entry points have to be found and
paired first). The console decides how slow it really is: Ryujinx's JIT on this PC is not the A57.

## True 60 fps: what the game allows (2026-10-29, the user's follow-up question)

- The engine has a 60 Hz setting: its device set-up `sub_825999B0` (a virtual method, vtable 0x820F0AB8 + 392) takes
  the presentation interval from its settings block's word +28 (1, 2, or anything else for immediate); NiGHTS passes
  2. The block is on the guest stack (0x7010FC90), filled by the caller.
- `nights_true60_interval = 1` (an experiment key in `src/nights_frame_hooks.cpp`, off) writes 1 there before the
  set-up: the game then presents at up to 49-60 fps on the PC, but **everything counted in frames runs twice as fast**
  (`runs/t60b`, a capture a second: the ESRB rating shows about 3 s instead of about 6, the epilepsy warning about 2 s
  instead of about 4; the movies keep their own speed). So the setting changes only the present rate: the game's
  logic steps once per frame with fixed amounts (1/30 and 1/60 appear in only 22 functions: no global time step).
- So a true 60 (the game's logic at 60 with its speed right) means halving every per-frame step of the game (movement,
  animation, timers, physics, scripts), function by function: not a setting, and not something to do blind. The way
  to 60 on the screen is the renderer's (`nights_present_60`, above): the game at 30, the screen at 60.
- **The frame** (found by counting the localized copies' calls per game frame, `banjo_hot_count`, `runs/cnt1`): the
  frame body `sub_82138B08` sets the phase word (0x841FED10) to 1 and runs the task system `sub_82138F60` (the input
  and manager steps `sub_82143FB0`, `sub_82144480`, `sub_82139FB8`, two managers, then the task list at 0x82790158:
  each task's exec method, `vtable + 8`, with `this` alone, no time argument), then sets the phase to 0 and runs the
  task system again (the draw pass), then the renderer's flush, the frame's timers and `sub_82137DB8`. Two calls of
  the task system a frame (update, draw), so functions such as `sub_8229C1F8` (works only in phase 1) ran 1.98 times
  a frame: one logic update a frame, not two 60 Hz ticks.
- So nothing in the engine scales the per-frame steps. A true 60 would be a port of its own: enumerate the task
  classes (log each task's vtable in the update pass), and for each find and halve its per-frame quantities
  (velocities, accelerations, animation frame advance, timers and wait counters, particle lifetimes, camera smoothing),
  plus the shared systems (the animation player, particles, the dream and link timers, scripts and cutscene timing,
  the music sync), keeping the scoring and the A-rank thresholds the same. Partly done it gives a game where some
  things run at double speed. Not done tonight; the renderer's 60 fps is the shipped answer.
- `nights_present_60` matching, second round: a draw with no exact match takes the n-th draw of the frame before with
  the same shaders and target (`LooseKey`), so particles whose vertex count changes still follow the interpolated
  camera (PC `runs/p60c`: 191,849 exact + 8,751 loose of 200,787 draws).
- Input lag: mode 0 (predict) adds none (the game's frame is shown when it is ready; the extra one is a prediction
  after it). Mode 1 (interpolate) shows the game's frame half a game frame (16.7 ms) later, though the in-between frame
  shown at the old time is already halfway to it. The shipped `banjo_late_input` and `banjo_deko3d_kick_submit` apply
  as at 30 fps.

## The final build in the emulator (2026-10-29, 05:00-06:05)

- emu8 (the PGO + LTO NRO, shipped settings, every verify key on, the guard on every hook's first 4,000 calls of a
  thread then 1 in 64): **0 differences on ARM** (857,530 guarded calls of the localized copies and hand natives, every
  verify check 0). The route stayed in the intro movie (the guard makes the decoder's copies run twice: ~1 fps).
- emu9 (the same, `nights_present_60` on): the whole emulated process froze 1 s after the title update was applied,
  before the first frame (even the SDK's 10 s profiler thread never wrote again): an emulator freeze, as emu1's. Open.
- emu10 (`nights_present_60`, predict): works on deko3d (295 of 300 game frames doubled in light scenes, 99 % of draws
  matched). **In heavy gameplay the game slowed to 21 fps** (the render thread needed ~40 ms a game frame: the game's
  own work, the replay and the half-frame wait, with only one frame of slack, kMaxFramesAhead = 1). Since the game
  steps per frame that is slow motion, so two changes: (1) the second present is scheduled, not waited for: the
  render thread goes on with the next frame's work and presents it between two items when it is due
  (`render_thread.cpp`); (2) when the game's frames come late (over 35 ms, smoothed) doubling pauses for 2 s, then
  4, 8 ... 32 s (reset after 30 s at speed): 60 where the console can afford it, the game's 30 where not.
- Emulator boot freezes: 3 of 13 Ryujinx runs (emu1, emu9, emu12) stopped before the first frame, each at a different
  point (emu12: only the host main thread alive at 100 % CPU, the guest memory not yet mapped, no game log; emu9: the
  whole process after the title update was applied; emu1: the game's log and captures stopped 43 s in). Each ran while
  other ports' emulators held the host's memory (5-9 GB free when they started). Not reproduced otherwise; to watch on
  the console.
- 60 fps back-off, final form: a frame counts as late when the frame before was doubled and it took over 35 ms (up to
  150 ms; longer is loading); eight late frames in a row pause doubling for 2 s, again within 10 s of resuming: up to
  32 s. Checked on the PC with `nights_present_60_test_delay_ms = 30`: doubling drops to 0-8 of 300 frames (the
  probes), the game holds 28.6-30 fps.
- **The boot freeze, bisected (07:20-07:40)**: emu12 and emu13 (the PGO + LTO NROs of 06:59 and 07:16) both froze at
  the same early point: before the logging system started (no "Log writer" thread, no game log), the host main thread
  at 100 %, after the first SaltyNX probe and before the APM clock set-up. A control run of the 04:31 NRO (package
  0435) booted normally right after (emuC), and so did a plain NRO (no PGO, no LTO) of the current source (emuP,
  `app/out/switch`). So the source is not at fault; the optimised build is. What changed for it: at 06:14 post_codegen
  ran again on the already processed tree (for the survey hook's direct call), which changed most generated files,
  so the PGO profile (taken at 03:56-04:12) no longer matched them. Re-taken from the current code (instrumented build
  07:46, session pgo3). Lessons: a PGO profile is retaken after any change to the generated code; post_codegen is
  best run after a fresh codegen (run again on its own output it mostly finds its own forms, "already", but some
  analyses differ: the frame-traffic pass saw 88 functions with the link register as data instead of 1, the bulk list
  took 99 copies instead of 91; the PC guard found 0 differences in that code).
- emu9's freeze (05:28) was with the same NRO as emu8, which ran: that one was the emulator's.
- **Why the profile went stale without warning**: ninja does not track the `.gcda` files, so an incremental build of
  `app/out/switch-pgo-use` recompiles only changed sources, against whatever profile it finds, and a new profile
  needs a clean build (`--target clean`, then the build: from scratch). Every rebuild of that folder from 05:11 on
  compiled the files that changed against the 04:12 profile; the 06:14 post_codegen changed most of them. Retaken:
  pgo3 (450 s in Ryujinx: the intro, Spring Valley, Night Over, both Dreams screens, Elliot's stage), 160 `.gcda`;
  the optimised folder cleaned and rebuilt from scratch.
- **The final optimised NRO in the emulator (08:10-08:20)**: emuF (shipped settings, every verify key, the guard):
  boots, Spring Valley, Night Over, the Dreams screen; verify 0 differences everywhere (up to 93,000 draws a window),
  the guard 1.92 M calls, 0 differences; 26-30 fps in gameplay. emuG (`nights_present_60`): the game held 28-30 fps
  (before the scheduling and the back-off: 18-21); doubling 300 of 300 frames in the lighter scenes, pausing itself in
  the heavy ones, where the extra frame cost 17-34 ms (more than the game's frame: its draws had no chain hint, so
  deko3d's fast path rebuilt everything). Hints added (`nights_present_60_hints`, on): the changed chunks between the
  extra frame's consecutive draws, as the game's own delta-chain draws carry.
- emuH (hints) and emuI (hints, and the texture / stream caches advanced after the extra frame): verify 0 differences,
  fast path 100 %, the game 28.6-30 fps all the way with 60 fps on; in the heavy scenes the extra frame still costs
  14-29 ms (emuG 17-34 ms), so it pauses there; elsewhere 285-300 of 300 frames doubled. The cost left in the heavy
  scenes is for a console profile to place (the replay of a frame is a draw-for-draw rerun on the render thread).
- Package for the hardware test: `dist/package/nights-nx_2026-10-29_0840` (NRO md5 229af7bd6d3f7988b4def6529acf9b31,
  commit 10ab3a50), the user's test from 08:40.
- **Resolution and SMAA on NiGHTS (09:00, the user's console test)**: banjo-nx's SMAA and its separate 3D render height
  both run after the frame's final composite draw, recognised by its pixel shader (`banjo_renut_smaa_after_ps`, Banjo's
  981A6A58F2ABFA42). NiGHTS has no such draw: the draw log (`banjo_renut_diag_draw_log_s`, `runs/dl1.log`) shows the
  whole 3D scene drawn straight into the 1280x720 back buffer with one pixel shader (6DDC2ECB177C6BCD, draws 0-175),
  then the HUD as blended quads (82E6A43836DB5864, draws 176-197), then one more quad (9EB041D3F259A4C6). So SMAA and
  `banjo_renut_render_height_*` do nothing on NiGHTS yet (a trigger "before the first HUD draw" would be needed). What
  does work: `banjo_renut_display_height_docked = 1080`, the whole frame (3D and HUD) at 1920x1080 when docked:
  emuD (Ryujinx switched to docked for the run, then back): display 1920x1080, captures 1920x1080, every screen right,
  the game 29-30 fps, 60 fps (predict) doubling 298-300 of 300 frames. `configs/renut_test_1080docked_60fps.toml`.
- **First console test (2026-10-29 09:10, the user; `/d/nights/*.mp4`, `/d/nights/logs`)**: 60 fps held on the console
  (298-300 of 300 frames doubled, the extra frame 4-13 ms, docked 1080p), but two glitches, both in the prediction:
  (1) the rings' balls scattered in every extra frame (the same mesh and shaders many times: paired with the frame
  before by order, which changes; and the loose match by shaders paired any draw with any); (2) Claris snapping in the
  Dreams menu every 17 game frames (her idle animation looping: the jump to the first pose carried half a frame
  further). Fixes: a draw moves whole only when it is the one draw of its kind in both frames; repeated ones move only
  the registers their vertex shader has the same in all its draws (the loose match is gone); and every register moves
  only while its motion is steady over three frames (its change within half of the change the frame before). PC:
  81 % of gameplay draws move whole, 13 % of the register moves held back as unsteady. Package
  `nights-nx_2026-10-29_0935` (also `configs/renut_test_1080docked_60fps_interpolate.toml`, mode 1).
- **Second console test (09:43, `dist/package/nights-nx_2026-10-29_0935/.../configs/logs`)**: no more flicker reported,
  but "most of the time the camera is updating at only 30": 20-45 % of the draws were repeated ones (the scenery: trees,
  flowers, rocks, rings, chips), and with each instance's matrix its own, none of their camera-dependent registers were
  shared, so the scenery stayed at 30 while NiGHTS moved at 60. Fix: the instances of a repeated draw are paired with
  the frame before (and that one with the frame before it) by their own constants: the nearest by the registers that
  differ between the group's draws, taken only when clearly nearer than the next (half the distance or less); refused
  pairs fall back to the shared registers. PC (`runs/pair1.log`): in gameplay 6,400-26,300 repeated draws a window
  paired, the extra frame 0.9-2.8 ms. Package `nights-nx_2026-10-29_0950`.
- **Third console test (09:58, `/d/nights/newlogs and videos`)**: smoother, but 3D objects and 2D sprites jitter.
  Measured on the clip (NiGHTS's purple pixels, 60 consecutive frames): her position comes in pairs (frames 0/1, 2/3
  ... the same), so the extra frames left her at 30 while the camera and the scenery moved at 60: the steady test held
  her back (in flight she turns and changes speed every frame). Fix: interpolating has no steady test at all (the frame
  halfway lies between two of the game's own: nothing can overshoot); predicting scales each register's step by its
  steadiness (1 - jerk / motion: all of it when steady, part in a turn, none for a loop's jump). What still steps at 30
  in both modes: sprites whose positions the game writes into their vertices each frame (sparkles, trails).
- **60 fps shelved (10:15, the user's call)**: "it's more trouble than it's worth: even in interpolate mode there is
  significant jittering". `nights_present_60` stays false (the code stays, inert with the key off); its three configs
  are gone from the package.
- **The release settings (10:20, the user: "SMAA on 720p handheld 1080p docked and port over double buffer and the
  double buffer input lag fix from mushihimesama")**:
  - SMAA on NiGHTS: `nights_renut_smaa_before_ps` (82E6A43836DB5864, the HUD's pixel shader). In DrawStateLocked,
    before the first HUD draw of a frame that follows a draw of the scene (any other draw), banjo-nx's post-composite
    work runs: SmaaRunLocked on the draw's colour target, then UiScaleAfterCompositeLocked (notes the composite for the
    3D height; scales the 3D to the display's size for the HUD when the heights differ). The draw's targets are
    gathered again after it (the UI target when scaled). The HUD's draws are marked composite in their draw summary,
    so they go the full way (the fast path, merging and sorting leave them alone: about 23 draws a frame), and so
    the trigger always sees the first of them. Frames of the HUD alone (2D menus) get no SMAA.
  - 1080p docked: display 1080 and 3D 1080 (code 2: the whole frame at 1.5x, no upscale); 720 handheld.
  - Double buffering: `nights_renut_double_buffer` (video.cpp, sa2-nx's form), 2 swapchain images.
  - The queue drain: `nights_renut_queue_drain` (display_sync.cpp, sa2-nx's: the display thread asks, PaceRenutSwap
    releases the next frame a refresh later; the late input's margin as well). One change for NiGHTS: sa2-nx judges
    the newest image at the first refresh after its present (60 fps). At NiGHTS's interval 2 the console's log has
    present -> display at 17-22 ms, so most images are finished only by the second refresh while still on time. Judged
    there, nearly every image would count as late and the drain would fire (one refresh of game time lost each time)
    every 5 s and on. Here an image is judged when found done, at its frame's last refresh (NextRelease's refresh count),
    or when a newer image comes first.
