# NiGHTS into Dreams on the ultimate base: the not-exact forms and their censuses

The base's switches that change results in rare cases (the base's docs/off-by-default.md section 2), each with NiGHTS's
census, its sites and a verdict. A form is on only when its census is done and written here; one that counts stays off
until the user decides. The PC build counts what each form would change while it runs the exact forms
(`banjo_diag_runtime_census`, the "[census]" lines every 10 s); the runs go through `tools/nights_pc_run.py --census`
and `tools/nights_census_read.py` (the base's run_pc.ps1 and cg13_census.ps1 in Python).

## The calling convention (before the machine copies' liveness is trusted)

Port 121's register liveness relies on the 360 convention. Run on NiGHTS's generated tree after post_codegen.py
(2026-11-22; one module, default.xex with the title update):

- `tools/cc_entry_census.py`: 17,774 functions, 5,024 reached through pointers; **no pointer-reached function reads v0
  or v32-v63 on entry** (the line that must say none). Those read v1 (3), v127 (3) and v12 (2) on entry: v1 is the
  first vector argument, v127 a non-volatile VMX128 register, v12 an argument. 240 functions read a vector register on
  entry at all (mostly v16-v31 and v127: the save helpers storing the non-volatiles).
- `tools/cc_exit_census.py`: 10,143 indirect call sites; **0 with a register of the exit bound read after the call**.
  `SASRT_REG_LIVENESS_EXIT_ABI` stays on (the base's default).
- setjmp 0x825872D0 / longjmp 0x82586EA0 (config/renut_funcs.toml): r1_local leaves the 11 functions with setjmp /
  longjmp alone, and the copy tool refuses a function that calls setjmp.

## The censuses (2026-11-22)

Three PC runs of 300 s with `banjo_diag_runtime_census = true` (`tools/nights_pc_run.py --census`), from a copy of the
save in `D:\nights\userdata` (Sega Saturn Dreams unlocked), the build of 4ac510ec (the PC build: CG-13 off, the mode
switching, so every switch is counted):

| Run | Route | Windows | CG-13: windows with a denormal (regions with one, a second) | Flush-mode switches a second |
|---|---|---|---|---|
| cg13_hd | title, Main Menu, Spring Valley (Brand New Dreams): Claris walks to the palace (`66:LSUP@18`), then random sticks; the Ideya capture, the hint screen, the Dreams select | 29 | 8: windows 6-13 (22, 98, 127, 187, 218, **626**, 339, 20) | 383,000 (in gameplay 480,000-505,000) |
| cg13_saturn | the same in Sega Saturn Dreams (`45:DOWN`) | 29 | 7: windows 6-12 (23, 54, 81, 286, 455, **569**, 336) | 371,000 |
| cg13_attract | no input: the intro, the title, the attract sequence | 27 | 18 (3-26 a second, from 80 s) | 3,000 |

Peak: 626 of about 191,000 FPU-mode regions a second (0.33 %), Spring Valley's first minute; the any-mode cold paths
(scalar results in VMX code that met a denormal) 0 in every window. The sites (`tools/census_sites.py --denormal`, the
switch back on that ended each stretch; banjo::FormsSelfCheck's own deliberate sites left out):

| Site (cumulative, cg13_hd / cg13_saturn) | What the stretch before it does with the value |
|---|---|
| `sub_822E3F28` at its first `vmaddfp` (11,982 / 13,775; 42-46 %) | `sub_822E3F28` is a point times a 4x4 matrix in VMX (`vmaddfp` chain); the stretch is its callers' scalar code before the call: `sub_822E1E78` hands the points over as words (`lwz` / `stw`, no float operation), `sub_822DB8C0` copies floats from its object (`lfs` / `stfs`, one `fadds`) into the stack matrix the transform reads |
| `n_822D93A8` (the localized copy of `sub_822D93A8`; 2,249 / 1,770) | three `lfs` / `stfs`: a float copy, then calls |
| `n_822DD9F8` at its call of `n_822D9CB0` (the main thread's hottest function; 2,147 / 2,470) | `lfs` x3 / `stfs` x3: a point copied from the object to the stack, then the matrix-stack point transform (VMX in the original) |
| `n_822DA840` (6 / 3), `n_822DA648` (1 / 2) | matrix code: `sub_822DA840` is VMX only (a 4x4 multiply); `sub_822DA648` a few `fadds` / `fmuls` feeding `vmulfp` / `vmsum4fp` |

**Verdict: CG-13 changes nothing these routes play.** Every stretch that met a denormal is a float copy or matrix data
on its way into VMX transforms, and the Xenon's VMX flushes a denormal input to zero itself: the one flush mode gives
the zero a little earlier. No division, normalisation, comparison with zero or physics step of such a value was found
at these sites. CG-13 stays ON in the Switch build (the base's default); the PC build keeps it off. Not covered: flying
as NiGHTS (the route's Claris did not reach the palace's centre), the bosses, the later dreams.

The other counters, the last cumulative line of each run (hd / saturn / attract):

| Counter | Counts | |
|---|---|---|
| #1 signalling NaNs at float copy stores | 0 / 0 / 0 | |
| #2 NaNs into float-to-integer conversions | **58 / 21** / 0 | counts |
| #17 signed divisions by zero, INT_MIN / -1, unsigned divisions by zero | 0 / 0 / 0 | |
| #41 float compares with a NaN | **42 / 35** / 0 | counts |
| #42 vector max/min with a differing lane | 0 of 1,132,272 / 0 of 1,133,872 / 0 of 6,664,804 | |
| #5 integer compares copying XER[SO] = 1 | 0 / 0 / 0 | |
| #19 traps (the trap log lines; 1,089 `REX_NX_TRAPS_ON` sites in the code) | 0 / 0 / 0 | |
| #3 FPSCR writes, #4 FPSCR reads | 0 / 0 / 0 | the rounding mode never written: fp_sign_ops' condition (up or down never written) is met |
| #32 compare-and-swap words | 386 words, 231 by one thread only (419,058 of 2,347,320 CAS) | evidence for the exact CAS forms |
| #38 card marks | 1,036 M (the copies' run marks 483 M; 3.66 M with a run pending on a page Renut reads, 0 writing the card) | |

## The forms

| Form | Census | Sites | Verdict | In the build |
|---|---|---|---|---|
| CG-13 (`BLUR_FPCR_ONE_MODE`) | three routes above: 8 / 7 / 18 windows, at most 626 regions a second (0.33 %) | float copies and matrix data into VMX transforms (`sub_822E3F28`'s callers, `sub_822D93A8`, `sub_822DD9F8`'s point copy, `sub_822DA840`, `sub_822DA648`) | harmless: the values go on into VMX code, which flushes them anyway | **ON on the Switch** (the base's default), off on the PC |
| `BLUR_NEAR_EXACT` #1, #5, #17, #19, #42 | 0 in every window of the three runs (#42: 0 of 8.9 M lanes) | none | cleared | **ON on the Switch** (`BLUR_NX_ONLY` = REX_NX_SNAN_PASS; REX_NX_DIV_NO_CHECK; REX_NX_XER_SO_ZERO; REX_NX_NO_TRAPS; REX_NX_VMINMAX_FAST), the PC exact |
| `BLUR_NEAR_EXACT` #2 (REX_NX_FCTI_NO_NAN), #41 (REX_NX_FCMP_NO_UN) | #2: 58 / 21 / 0; #41: 42 / 35 / 0 | not looked at | count: off until the user decides | off |
| Port 72's VMX forms (`SASRT_VMX_FAST_ESTIMATES`, `SASRT_VMX_FUSED_MADD`; ON in the base at the user's word) | no PC census can count them (AArch64 only; the PC computes the exact forms). NiGHTS's exposure, counted statically by post_codegen.py: 546 `vmaddfp`, 31 `vnmsubfp`, 6 `vrefp`, 5 `vrsqrtefp`, 1 `vlogefp` as macros in 44 files | `sub_822E3F28` (the point x matrix transform) is one of them | sa2-nx-ult found the fused multiply-add made its two-player attract demo drift from 1.0.6 in the emulator (a replay); NiGHTS has no replay that the PC could compare, and the emulator is not run here | ON (the base's default); **a decision for the user**: a console A/B against 1.0.2, or `-DSASRT_VMX_FUSED_MADD=OFF` as SA2 took it |
| `BLUR_NX_R1_ASSUME` (#9) | code generated with it (2026-11-22): r1_preserve proves 17,610 of 17,774 functions keep r1 (17,269 without it), r1_local 48,851 assumed call sites, 607 reloads; the PC build's REX_R1_CHECK tests every call the assumption covers | | verify_ne (300 s, the census route, every copy guarded): **223 million calls checked, 0 differences** | **on** (tools/post_codegen.py's default; `=0` gives the exact form) |
| `BLUR_LOCALIZE_ABI_TRUST` (#232) | the copies take the 360 ABI's non-volatile registers as a real call left them: 134 reloads left out after 18 real calls. The case it changes is a callee that breaks the ABI (hand-written assembly, setjmp / longjmp): the copy tool refuses setjmp's callers, and the calling-convention censuses above are clean | | verify_ne: the guard compared 69.6 million calls of the 62 natives and 57 of 98 bulk copies reached, **0 differences** (NiGHTS's hand natives: memcpy 14.5 M, memset 1.2 M, the transforms 4.0 M and 2.9 M, the buffer reset 0.3 M calls); flush-mode flow 92 M, stack forwards 238 k, folded loads 249 M: 0 | **on** (tools/post_codegen.py's default) |
| `BANJO_GEN_LR_MOVES_ELIDE`, `BANJO_GEN_NV_SAVES_ELIDE`, `BANJO_GEN_BACK_CHAIN_ELIDE` | not run: their test is a frame-exact lockstep comparison of every scene the PC reaches (the base's rule after sa2-nx's tint), and this tree has no lockstep recorder (sa2-nx-ult grafted its own; the base has none). post_codegen.py marked 7,836 `mflr` with their saves and 8,334 `mtlr`, 7,671 non-volatile saves and 13,028 back-chain stores | | not cleared | off (the base's default) |
| `SASRT_HOT_LR_ELIDE` | not run: its census is a guard build with it on (banjo-nx-ult found 2 differences at copies' LR save slots) | | not cleared | off (the base's default) |
| `BANJO_GENERATED_NO_TRAPPING_MATH` | no census of its own (below) | the 13 FPSCR-writing files keep `-ftrapping-math` | the guest wrote and read no FPSCR in the census runs | **on in the Switch build** (with CG-13), as the base's PC |

`BANJO_GENERATED_NO_TRAPPING_MATH`: no census of its own. It lets the compiler move a float operation across a write of
the host's float mode; with CG-13 the only such writes left in game code are the guest's FPSCR writes, in the 13 files
tools/fp_env_files.py lists (app/generated/fp_env_files.txt), which keep `-ftrapping-math`; NiGHTS's three census runs
made no FPSCR write and no FPSCR read (#3, #4). ON in the Switch build with CG-13 (banjo-nx-ult's form); the PC build
cannot test it (it keeps CG-13 off, and the option needs it).

## For the console step

The measurement tools that want a console profile (the base's tools/codegen_census.py, generated_code_census.py) and
the build-side ones (loop_body_census.py, arm_cost_census.py) are for the console session, with the user's word. The
cleared forms change every generated file: a PGO profile is trained with the settings the build ships with.

The verify run (verify_ne) guards every call of every copy, so the game runs at about 1.6 fps in gameplay: 300 s
covered the menus and the start of Spring Valley's gameplay (about 500 draws a frame), fewer gameplay frames than the
census runs. A longer or second verify route (Sega Saturn Dreams, flying as NiGHTS) would widen it.
