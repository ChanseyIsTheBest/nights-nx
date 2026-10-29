# renut - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(renut LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# banjo-nx (from peggle2-nx, 2026-10-13): -mearly-ra=none for everything compiled for the console. devkitA64 GCC 16.1
# runs an AArch64 "early register allocation" pass at -O2 and above (-mearly-ra=all). In Peggle 2's console build it
# dropped 38 of the table lookups of the XDK's streaming copy (the two-register lookup of the NEON vperm helper,
# rex/ppc/intrinsics.h: V2x16QI pseudos that need consecutive registers) and overwrote the register holding a
# constant: large textures came out as garbage on the console only (clang builds the PC code correctly, so no PC check
# can see it). Banjo's ELF of 2026-10-13 shows the same routine complete (159 lookups) and no self-indexed lookup
# anywhere, but the trigger is there. Here, before the SDK is added, so its libraries and any later LTO link get it
# too (a toolchain-file flag would only reach a fresh build folder: CMake caches those). Checked after a build: the
# flag in every compile command, and no `tbl vA, {vB}, vB` in a recompiled function. BANJO_SWITCH_EARLY_RA=ON puts
# the compiler's default back.
option(BANJO_SWITCH_EARLY_RA "Leave GCC's AArch64 early register allocation pass on (Switch; miscompiles, see above)" OFF)
# Line tables (-g1) for the three hot renderer files, so tools/switch_hot_lines.py can say where inside a function a
# console profile's samples fall. -g does not change the generated code: an ELF built with this ON has every function
# at the address of the same sources built with it OFF, so it names the samples of a run of the normal NRO.
option(BANJO_SWITCH_RENDERER_LINES "Switch: line tables for the hot native renderer files (profiling aid)" OFF)
if(REXGLUE_PLATFORM_SWITCH AND NOT BANJO_SWITCH_EARLY_RA)
    add_compile_options(-mearly-ra=none)
    add_link_options(-mearly-ra=none)
endif()
# banjo-nx (package 27, codegen round): atomics inline. GCC's default for AArch64 calls out-of-line helpers
# (__aarch64_cas4_acq, __aarch64_ldadd8_rel, ...) that test for LSE atomics at run time; the A57 has none, so every
# atomic was a call, a load and a branch before the same load-exclusive / store-exclusive loop this writes inline (75
# call sites in the ELF of package 26: shared_ptr counts, the SDK's and the renderers' atomics). The same instructions.
option(BANJO_SWITCH_OUTLINE_ATOMICS "Switch: GCC's out-of-line atomic helpers (its default; OFF writes them inline)" OFF)
if(REXGLUE_PLATFORM_SWITCH AND NOT BANJO_SWITCH_OUTLINE_ATOMICS)
    add_compile_options(-mno-outline-atomics)
endif()

include(generated/rexglue.cmake)

# banjo-nx (from sa2-nx): the optimised codegen. The build re-runs the generator by itself whenever an input in
# codegen.d changes (renut_config.toml with the codegen keys, the xex, the generator binary). That rewrites
# app/generated and would silently drop the post-codegen steps (no-volatile pch + loop barriers, direct calls): they are
# appended to the same command (docs/enhancements.md, "Build-time"). The Switch cross build has no generator target.
if(TARGET renut_codegen)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/codegen.build.stamp" APPEND
        COMMAND "${CMAKE_COMMAND}" -E env "REXSDK_DIR=${REXSDK_DIR}"
                "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/post_codegen.py")
endif()
# The codegen flavour (written by tools/post_codegen.py) names the build in the "[enhancements]" log line.
set(BANJO_FLAVOR_FILE "${CMAKE_CURRENT_SOURCE_DIR}/generated/codegen_flavor.txt")
set(BANJO_BUILD_FLAVOR "classic codegen")
if(EXISTS "${BANJO_FLAVOR_FILE}")
    file(READ "${BANJO_FLAVOR_FILE}" BANJO_BUILD_FLAVOR)
    string(STRIP "${BANJO_BUILD_FLAVOR}" BANJO_BUILD_FLAVOR)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BANJO_FLAVOR_FILE}")
endif()
set_source_files_properties(src/cpu/banjo_enhancements.cpp PROPERTIES
    COMPILE_DEFINITIONS "BANJO_BUILD_FLAVOR=\"${BANJO_BUILD_FLAVOR}\"")
# The native game-function replacements reproduce the generated code's host flush mode at exit; with the codegen option
# fp_moves_modeless (renut_config.toml) a floating-point move no longer switches it (src/cpu/banjo_reverb_native.cpp).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/renut_config.toml" BANJO_FP_MOVES_MODELESS_LINE REGEX "^fp_moves_modeless *= *true")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/renut_config.toml")
if(BANJO_FP_MOVES_MODELESS_LINE)
    set_source_files_properties(src/cpu/banjo_reverb_native.cpp PROPERTIES COMPILE_DEFINITIONS "BANJO_FP_MOVES_MODELESS=1")
endif()



# nights-nx (2026-10-28): banjo-nx's sources split in two. RENUT_SOURCES are the port's own and the parts every port
# shares (the app, the profilers, the enhancement framework, the hot-copy shards, the captures). NIGHTS_NB_SOURCES are
# Banjo-Kazooie: Nuts & Bolts' game code: its gameplay hooks and menus, banjo-nx's PM4-ring renderer (src/me, hooked at
# N&B's Direct3D addresses), its XAudio natives and pool-lock fix (N&B's named functions). They name N&B functions and
# do not build for NiGHTS; src/nights_dropped_cvars.cpp defines the settings of theirs the shared code still reads.
set(RENUT_SOURCES
    src/main.cpp
    src/nights_dropped_cvars.cpp
    # NiGHTS's frame hook on D3DDevice_Swap (frame count, Renut's present and pacing).
    src/nights_frame_hooks.cpp
    # The game's language from renut.toml "language" (from sa2-nx).
    src/nights_language.cpp
    # PGO profile collection (BANJO_PGO=generate on the Switch; empty stubs otherwise). From sa2-nx.
    src/banjo_pgo.cpp
	src/renut_engine/shader_dump.cpp
	src/renut_engine/texture_tools.cpp
    # CPU/GPU profiler ported from UnleashedRecomp-NX (see src/profiler/profiler.h).
    src/profiler/profiler.cpp
    # On/off switches and the startup summary of every performance enhancement (src/cpu/banjo_enhancements.h).
    src/cpu/banjo_enhancements.cpp
    # sa2-nx's CPU rounds: the PC side of the post-codegen self-checks and the censuses (empty on the Switch), and
    # the start-up check of every instruction form that only one processor has.
    src/cpu/banjo_codegen_checks.cpp
    src/cpu/banjo_forms_selfcheck.cpp
    # Native copies of hot game functions (from sa2-nx): tools/localize_native.py writes them into generated/hot from
    # src/cpu/hot/localized.txt; each has its own key (banjo_cpu_native_*) and a guard that runs both versions.
    src/cpu/banjo_hot_guest.cpp
    # 2026-10-22 (bejeweled3-nx's method): the bulk list's copies (src/cpu/hot/localized_bulk.txt), their hooks spread
    # over these files so they compile in parallel.
    src/cpu/banjo_hot_shard_0.cpp
    src/cpu/banjo_hot_shard_1.cpp
    src/cpu/banjo_hot_shard_2.cpp
    src/cpu/banjo_hot_shard_3.cpp
    src/cpu/banjo_hot_shard_4.cpp
    src/cpu/banjo_hot_shard_5.cpp
    src/cpu/banjo_hot_shard_6.cpp
    src/cpu/banjo_hot_shard_7.cpp
    src/cpu/banjo_hot_shard_8.cpp
    src/cpu/banjo_hot_shard_9.cpp
    src/cpu/banjo_hot_shard_10.cpp
    src/cpu/banjo_hot_shard_11.cpp
    src/profiler/banjo_pc_profiler.cpp
    # PNG captures of the presented game image (nfsmw_captura_cada_s), from nfsmw-nx: for testing.
    src/native/nfsmw_nativo_captura.cpp
)
set(NIGHTS_NB_SOURCES
    src/renut_engine/hooks.cpp
    src/renut_engine/StopNSwap.cpp
    src/renut_engine/frameHooks.cpp
    src/renut_engine/cvar_menu.cpp
    src/renut_engine/FPS.cpp
    src/renut_engine/frontend_debug_menu.cpp
    src/renut_engine/townsfolk.cpp
    src/me/me_native_system.cpp
    src/me/me_ring_wait.cpp
    src/me/masseffect/masseffect_native_draws.cpp
    src/me/masseffect/masseffect_deferred_recording.cpp
    src/me/masseffect/masseffect_native_targets.cpp
    src/me/masseffect/masseffect_native_shaders.cpp
    src/me/masseffect/masseffect_native_textures_pool.cpp
    src/me/masseffect/masseffect_shader_library.cpp
    src/me/masseffect/me_masseffect_glue.cpp
    src/me/banjo_me_hooks.cpp
    src/me/banjo_renderdoc.cpp
    src/cpu/banjo_pool_lock.cpp
    src/cpu/banjo_reverb_native.cpp
    src/cpu/banjo_xaudio_native.cpp
    src/native/banjo_shader_harvest.cpp
)

# Keyboard + mouse needs a desktop window; the Switch gets no-op hooks instead.
if(REXGLUE_PLATFORM_SWITCH)
    list(APPEND RENUT_SOURCES
        src/renut_engine/mnk_controls_switch.cpp
        # Unleashed's sampling CPU profiler and stall watchdog (libnx only).
        src/profiler/cpu_profiler_switch.cpp
        src/profiler/stall_watch_switch.cpp)
else()
    list(APPEND RENUT_SOURCES src/renut_engine/mnk_controls.cpp
        # Switch profiler counters the renderer calls (no-ops on PC; banjo-nx's file, nothing N&B in it).
        src/me/me_pc_stubs.cpp
        # Sampling CPU profiler for PC tests, from nfsmw-nx (nfsmw_perfil_pc_desde_s; logs/perfil_pc.csv).
        src/profiler/nfsmw_perfil_pc.cpp)
endif()

if(WIN32)
# nights-nx (2026-10-29): the PC exe's icon only where icon/app.ico is (banjo-nx's, Banjo's art: not in the published
# sources).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icon/app.ico")
list(APPEND RENUT_SOURCES icon/app.rc)
endif()
    add_executable(renut WIN32 ${RENUT_SOURCES})
else()
    add_executable(renut ${RENUT_SOURCES})
endif()

# GPU_PLUGINS stages the emulated-GPU backend (rexgpu-xenos.dll) next to the exe.
# It is only used when the gpu_plugin cvar is "xenos"; empty means no GPU at all
# (the game runs but the screen stays black).
rexglue_setup_target(renut GPU_PLUGINS xenos)

# banjo-nx (2026-10-17): Ryujinx starts NROs without argv[0]; the SDK then takes the NRO's usual place on the SD
# (sdk/src/core/filesystem_posix.cpp). A console's hbloader always passes the path, so nothing changes there.
if(REXGLUE_PLATFORM_SWITCH AND TARGET rexcore)
    target_compile_definitions(rexcore PRIVATE REX_SWITCH_FALLBACK_EXE="/switch/nights-nx/nights-nx.nro")
endif()

# banjo-nx (2026-10-16): Renut's Direct3D-level renderer as a second renderer, chosen at boot (banjo_renderer = "renut";
# cmake/banjo_renut_gpu.cmake, docs/renut-renderer-port.md). OFF builds banjo-nx's own renderer only.
option(BANJO_RENUT_RENDERER "Build Renut's native renderer in (banjo_renderer = renut)" ON)
if(BANJO_RENUT_RENDERER)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/banjo_renut_gpu.cmake")
endif()

# The SDK compiles its own targets with these (sdk/CMakeLists.txt) but they did not reach the app's: without them GCC
# (Switch) fuses separate guest multiplies and adds into FMAs across statements (-ffp-contract=fast is its default),
# so the recompiled game's floating point drifted from the Xbox 360 and from the PC build (x86-64-v2 has no FMA), and
# type-punned guest register/memory accesses relied on strict aliasing. The native game-function replacements are
# checked against the recompiled code built this way.
foreach(_banjo_target renut renut_recomp)
    if(TARGET ${_banjo_target})
        target_compile_options(${_banjo_target} PRIVATE -ffp-contract=off -fno-strict-aliasing)
    endif()
endforeach()

# banjo-nx (masseffect-nx ships these, futari-nx measured them): the generated game code without errno bookkeeping in
# math functions (sqrt and friends become one instruction instead of a possible libm call) and without assuming a
# floating-point operation can trap. IEEE results are unchanged (no -ffinite-math-only: NaN and infinity checks stay);
# only errno and trap semantics, which the recompiled code never observes. Matters on the Switch (GCC's defaults are
# -fmath-errno -ftrapping-math); clang for the PC mostly assumes them already. -DBANJO_GENERATED_MATH_FLAGS=OFF to drop.
# 2026-10-12 (sa2-nx's audit): only -fno-math-errno. -fno-trapping-math lets GCC run a float operation earlier than the
# code does (out of a branch or a loop), and the recompiled code switches the host's flush mode in between (msr fpcr),
# which the compiler does not know a float operation depends on: an operation moved in front of a mode check would
# compute a denormal operand in the wrong mode. It bought sa2-nx at most 0.5 % of one file. Not exact, so off;
# -DBANJO_GENERATED_NO_TRAPPING_MATH=ON puts it back for a measurement.
option(BANJO_GENERATED_MATH_FLAGS "-fno-math-errno on the generated game code" ON)
option(BANJO_GENERATED_NO_TRAPPING_MATH "-fno-trapping-math on the generated game code (not exact, see the comment)" OFF)
if(BANJO_GENERATED_MATH_FLAGS AND TARGET renut_recomp AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(renut_recomp PRIVATE -fno-math-errno)
    if(BANJO_GENERATED_NO_TRAPPING_MATH)
        target_compile_options(renut_recomp PRIVATE -fno-trapping-math)
    endif()
endif()

# The localized native copies (src/cpu/banjo_hot_guest.cpp includes generated/hot) are game code compiled in the app:
# the same rules as renut_recomp for that one file (sa2-nx's findings). -fno-math-errno: GCC does not inline between
# two functions that compute with floats and differ in it, and the guard compares a copy with code built that way.
# -ffp-contract=off is already on the whole target (above). On the PC the same census definition as the game code, so
# the any-mode helper counts stay complete.
# 2026-10-22: the bulk shards are the same kind of code (BANJO_HOT_SOURCES: every file that compiles copies).
set(BANJO_HOT_SOURCES src/cpu/banjo_hot_guest.cpp
    src/cpu/banjo_hot_shard_0.cpp
    src/cpu/banjo_hot_shard_1.cpp
    src/cpu/banjo_hot_shard_2.cpp
    src/cpu/banjo_hot_shard_3.cpp
    src/cpu/banjo_hot_shard_4.cpp
    src/cpu/banjo_hot_shard_5.cpp
    src/cpu/banjo_hot_shard_6.cpp
    src/cpu/banjo_hot_shard_7.cpp
    src/cpu/banjo_hot_shard_8.cpp
    src/cpu/banjo_hot_shard_9.cpp
    src/cpu/banjo_hot_shard_10.cpp
    src/cpu/banjo_hot_shard_11.cpp
)
if(BANJO_GENERATED_MATH_FLAGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE ${BANJO_HOT_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -fno-math-errno)
endif()
if(NOT REXGLUE_PLATFORM_SWITCH)
    set_property(SOURCE ${BANJO_HOT_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS REX_DIET_CENSUS_ON)
endif()

# sa2-nx's CPU rounds of 2026-10-12 (docs/enhancements.md, "sa2-nx's CPU rounds"): one switch back per post-codegen
# step (tools/post_codegen.py). OFF compiles the statements as they were before the step, on every platform, so a PC
# build can take one step out to find which of them a difference comes from.
option(BANJO_GEN_RO_CONSTANTS "Loads from the image's read-only data as constants" ON)
option(BANJO_GEN_FLOAT_LOADS "Single-precision loads straight into a float register" ON)
# The three stack-frame elisions of tools/frame_traffic.py are OFF: they change what stale data the guest's stack
# frames hold; sa2-nx found a game that reads a frame word it never wrote (characters tinted). NOT exact.
option(BANJO_GEN_LR_MOVES_ELIDE "No moves of the guest link register in prologues and epilogues (NOT exact)" OFF)
option(BANJO_GEN_NV_SAVES_ELIDE "No prologue saves of non-volatile registers that are locals (NOT exact)" OFF)
option(BANJO_GEN_BACK_CHAIN_ELIDE "No back-chain stores of the stack frames (NOT exact for a guest stack walk)" OFF)
option(BANJO_GEN_LEAF_LOCALS "ctx registers of leaf functions with loops as locals" ON)
option(BANJO_GEN_LOOP_LOCALS "ctx registers of call-free loops as locals for the length of the loop" ON)
option(BANJO_GEN_COLD_TRAPS "The trap helper out of line and cold" ON)
option(BANJO_GEN_PROMOTED_CALLS "Guarded direct calls at the promoted indirect call sites" ON)
# From sasrt-nx's codegen round of 2026-10-14 (tools/*.py named below; each tested in sdk/tests).
option(BANJO_GEN_VPERM1 "A vperm of one vector as a one-register lookup (tools/vperm_one_source.py)" ON)
option(BANJO_GEN_FP_SIGN_OPS "fneg / fabs / fnabs as float operations (tools/fp_sign_ops.py)" ON)
option(BANJO_GEN_LEAF_DEAD "Leaf write-backs no caller can read are skipped (tools/leaf_dead_stores.py)" ON)
# The PC test of BANJO_GEN_LEAF_DEAD: a skipped register gets a poison value instead (0xDEADBEEF..., a signalling NaN),
# so a reader the analysis missed breaks the game visibly. Never for a build that ships.
option(BANJO_GEN_LEAF_DEAD_POISON "PC test: poison the skipped leaf write-backs" OFF)
option(BANJO_GEN_DEAD_DEFS "Guest register writes no instruction can read are left out (tools/dead_ctx_defs.py)" ON)
# The PC test of BANJO_GEN_DEAD_DEFS: the register gets a poison value instead. Never for a build that ships.
option(BANJO_GEN_DEAD_DEFS_POISON "PC test: poison the left-out register writes" OFF)
option(BANJO_GEN_SINGLE_FLOATS "Single-precision arithmetic in float where its inputs are singles (tools/single_floats.py)" ON)
option(BANJO_GEN_INLINE_MEMCALLS "memcpy / memset of a constant size up to 64 bytes inline (tools/inline_memcalls.py)" ON)
option(BANJO_GEN_VEC_UNALIGNED "stvlx / stvrx as a few stores instead of byte loops (tools/vec_unaligned_stores.py)" ON)
option(BANJO_GEN_NARROW_LOOP_BARRIER "Loop barriers clobber the guest window only, not all memory (tools/loop_barriers.py, from marathon-nx)" ON)
option(BANJO_GEN_DCBT_PREFETCH "The dcbt / dcbtst hints as host prefetches (generator dcbt_prefetch; OFF: nothing)" ON)
# 2026-10-16 (CPU codegen agent): the vector registers of the loop and leaf locals (tools/loop_locals.py,
# tools/leaf_locals.py) as values instead of references to ctx, so a vector result is not stored to ctx on every loop
# iteration. Measured on all 213 files: -1.42 % weighted by the console's samples, +0.05 % instructions (294 of the 481
# functions it touches cheaper, 54 dearer; the XAudio mixers -15 to -19 %). Safe with -mearly-ra=none (the cause of
# peggle2-nx's fault of 2026-10-13, see loop_locals.py).
option(BANJO_GEN_VECTOR_LOCALS_BY_VALUE "Vector registers of the loop and leaf locals as values, not ctx references" ON)
# 2026-10-16 (CPU codegen agent): read-only constants found over the control-flow graph (tools/ro_constants_cfg.py):
# 2,730 more loads folded, mostly lfs in loops. The PC build compares each with the real load (RexRoCheck).
option(BANJO_GEN_RO_CFG "Read-only constants followed over the control-flow graph (tools/ro_constants_cfg.py)" ON)
# 2026-10-16 (the CPU codegen agent's aggressive round, docs/codegen-enhancements.md steps 23 and 25-29): each has its switch
# back here; the PC build checks the r1 mirror and the redundant link-register write-backs (REX_R1_CHECK,
# REX_LR_WRITE_SAME_CHECK, reported in the [verify] line of banjo_codegen_checks.cpp).
option(BANJO_GEN_FLUSH_HOIST "Loop-header flush-mode checks in front of the loop (tools/flush_hoist.py)" ON)
option(BANJO_HOT_LAZY_LOG10 "The XAudio limiter's localized copy computes log10 only where it is read (tools/limiter_lazy_log10.py)" ON)
option(BANJO_GEN_SWITCH_THREAD "The script interpreters' jump tables as threaded computed gotos (tools/switch_goto.py)" ON)
option(BANJO_GEN_LR_WRITE_SAME "Link-register write-backs proven redundant compile to nothing (tools/lr_preserve.py)" ON)
option(BANJO_GEN_R1_LOCAL "Guest r1 as a local mirror in the chosen functions (tools/r1_local.py)" ON)
# Package 27's codegen round (docs/codegen-enhancements.md): each a post-codegen step with its switch back.
option(BANJO_GEN_VSEL_NEON "VMX vsel as one NEON bit select (tools/vsel_neon.py)" ON)
option(BANJO_GEN_HOT_LAYOUT "The console profile's functions declared hot, gathered by the linker (tools/hot_layout.py)" ON)
option(BANJO_GEN_INLINE_LEAVES "Inline copies of the tiny leaf functions each file calls in another (tools/inline_leaves.py)" ON)
# 2026-10-19 (CPU proposal 1): every guest store outside the stack marks its page's store card (tools/store_cards.py);
# Renut proves its cached vertex and index copies by them (banjo_renut_store_cards). OFF compiles the marks out (Renut
# then hashes as before: app/src/cpu/banjo_hot_guest.cpp tells it).
option(BANJO_GEN_STORE_CARDS "Guest stores outside the stack mark their page's store card (tools/store_cards.py)" ON)
set(_banjo_round_definitions "")
if(NOT BANJO_GEN_VPERM1)
    list(APPEND _banjo_round_definitions REX_VPERM1_OFF)
endif()
if(NOT BANJO_GEN_FP_SIGN_OPS)
    list(APPEND _banjo_round_definitions REX_FP_SIGN_INTEGER)
endif()
if(NOT BANJO_GEN_LEAF_DEAD)
    list(APPEND _banjo_round_definitions REX_LEAF_DEAD_KEEP)
endif()
if(BANJO_GEN_LEAF_DEAD_POISON)
    list(APPEND _banjo_round_definitions REX_LEAF_DEAD_POISON)
endif()
if(NOT BANJO_GEN_DEAD_DEFS)
    list(APPEND _banjo_round_definitions REX_DEAD_DEFS_KEEP)
endif()
if(BANJO_GEN_DEAD_DEFS_POISON)
    list(APPEND _banjo_round_definitions REX_DEAD_DEFS_POISON)
endif()
if(NOT BANJO_GEN_SINGLE_FLOATS)
    list(APPEND _banjo_round_definitions REX_SINGLE_FLOATS_OFF)
endif()
if(NOT BANJO_GEN_INLINE_MEMCALLS)
    list(APPEND _banjo_round_definitions REX_INLINE_MEMCALLS_OFF)
endif()
if(NOT BANJO_GEN_VEC_UNALIGNED)
    list(APPEND _banjo_round_definitions REX_VEC_UNALIGNED_LOOPS)
endif()
if(NOT BANJO_GEN_RO_CONSTANTS)
    list(APPEND _banjo_round_definitions REX_RO_CONSTANTS_OFF)
endif()
if(NOT BANJO_GEN_FLOAT_LOADS)
    list(APPEND _banjo_round_definitions REX_FLOAT_LOADS_INTEGER)
endif()
if(BANJO_GEN_LR_MOVES_ELIDE)
    list(APPEND _banjo_round_definitions REX_LR_MOVES_ELIDE)
endif()
if(BANJO_GEN_NV_SAVES_ELIDE)
    list(APPEND _banjo_round_definitions REX_NV_SAVES_ELIDE)
endif()
if(BANJO_GEN_BACK_CHAIN_ELIDE)
    list(APPEND _banjo_round_definitions REX_BACK_CHAIN_ELIDE)
endif()
if(NOT BANJO_GEN_LOOP_LOCALS)
    list(APPEND _banjo_round_definitions REX_LOOP_LOCALS_OFF)
endif()
if(NOT BANJO_GEN_LEAF_LOCALS)
    list(APPEND _banjo_round_definitions REX_LEAF_LOCALS_OFF)
endif()
if(NOT BANJO_GEN_COLD_TRAPS)
    list(APPEND _banjo_round_definitions "REX_TRAP_ATTR=")
endif()
if(NOT BANJO_GEN_PROMOTED_CALLS)
    list(APPEND _banjo_round_definitions REX_PROMOTED_CALLS_OFF)
endif()
if(NOT BANJO_GEN_NARROW_LOOP_BARRIER)
    list(APPEND _banjo_round_definitions REX_LOOP_BARRIER_FULL)
endif()
if(NOT BANJO_GEN_DCBT_PREFETCH)
    list(APPEND _banjo_round_definitions "REX_PREFETCH(x,write)=")
endif()
if(BANJO_GEN_VECTOR_LOCALS_BY_VALUE)
    list(APPEND _banjo_round_definitions REX_VECTOR_LOCALS_BY_VALUE)
endif()
if(NOT BANJO_GEN_RO_CFG)
    list(APPEND _banjo_round_definitions REX_RO_CFG_OFF)
endif()
if(NOT BANJO_GEN_FLUSH_HOIST)
    list(APPEND _banjo_round_definitions REX_FLUSH_HOIST_OFF)
endif()
if(NOT BANJO_HOT_LAZY_LOG10)
    list(APPEND _banjo_round_definitions BANJO_LAZY_LOG10_ON=0)
endif()
if(NOT BANJO_GEN_SWITCH_THREAD)
    list(APPEND _banjo_round_definitions REX_SWITCH_THREAD_OFF)
endif()
if(NOT BANJO_GEN_LR_WRITE_SAME)
    list(APPEND _banjo_round_definitions REX_LR_WRITE_SAME_OFF)
endif()
if(NOT BANJO_GEN_R1_LOCAL)
    list(APPEND _banjo_round_definitions REX_R1_LOCAL_OFF)
endif()
if(NOT BANJO_GEN_VSEL_NEON)
    list(APPEND _banjo_round_definitions REX_VSEL_OFF)
endif()
if(NOT BANJO_GEN_HOT_LAYOUT)
    list(APPEND _banjo_round_definitions REX_HOT_LAYOUT_OFF)
endif()
if(NOT BANJO_GEN_INLINE_LEAVES)
    list(APPEND _banjo_round_definitions REX_INLINE_LEAVES_OFF)
endif()
if(NOT BANJO_GEN_STORE_CARDS)
    list(APPEND _banjo_round_definitions REX_STORE_CARDS_OFF)
endif()
if(NOT REXGLUE_PLATFORM_SWITCH)
    list(APPEND _banjo_round_definitions REX_R1_CHECK REX_LR_WRITE_SAME_CHECK)
endif()
# PC builds of the game code count the executions of the any-mode float helpers (rex/ppc/diet_ops.h), for
# banjo_diag_runtime_census; nothing on the Switch.
if(NOT REXGLUE_PLATFORM_SWITCH AND TARGET renut_recomp)
    target_compile_definitions(renut_recomp PRIVATE REX_DIET_CENSUS_ON)
endif()
if(REXGLUE_PLATFORM_SWITCH)
    # The Switch-only forms of the first round (from sasrt-nx), each with its switch back:
    #   - no frame pointer in the game code: AArch64 GCC keeps the x29 frame chain in every non-leaf function by
    #     default, which costs the set-up instruction and one of the callee-saved registers the recompiled code is
    #     short of (27,354 game functions set one up in the build of 2026-10-12). The console profiler's pc samples
    #     are unaffected; its caller chains (switch_perf.cpp walks x29, bounds-checked) end at a game function.
    #   - flush-mode checks the control flow fixes compile to nothing (tools/flush_mode_flow.py);
    #   - guest compare-and-swap without the trailing `dmb ish` (tools/guest_cas.py; run time:
    #     banjo_cpu_guest_cas_no_barrier);
    #   - guest vector loads and stores reversed with REV64 + EXT (tools/vec_byteswap.py);
    #   - constant word shuffles as one permutation (tools/pch_neon_shuffles.py);
    #   - vperm and the float-to-integer conversions as their NEON instruction (rex/ppc/intrinsics.h).
    option(BANJO_SWITCH_OMIT_FRAME_POINTER "Game code without the x29 frame chain (Switch)" ON)
    option(BANJO_SWITCH_FLUSH_FLOW "Flush-mode checks proven by control flow compile to nothing (Switch)" ON)
    option(BANJO_SWITCH_GUEST_CAS "Guest compare-and-swap without the trailing dmb (Switch)" ON)
    option(BANJO_SWITCH_VEC_BYTESWAP "Guest vector byte reversal as REV64 + EXT (Switch)" ON)
    option(BANJO_SWITCH_NEON_SHUFFLES "Constant word shuffles as one permutation (Switch)" ON)
    option(BANJO_SWITCH_NEON_VMX_HELPERS "vperm and float-to-integer conversions as NEON instructions (Switch)" ON)
    if(NOT BANJO_SWITCH_FLUSH_FLOW)
        list(APPEND _banjo_round_definitions REX_FLUSH_FLOW_OFF)
    endif()
    if(NOT BANJO_SWITCH_GUEST_CAS)
        list(APPEND _banjo_round_definitions REX_GUEST_CAS_LEGACY)
    endif()
    if(NOT BANJO_SWITCH_VEC_BYTESWAP)
        list(APPEND _banjo_round_definitions REX_BSWAP128_TABLE)
    endif()
    if(NOT BANJO_SWITCH_NEON_SHUFFLES)
        list(APPEND _banjo_round_definitions REX_SIMDE_SHUFFLES)
    endif()
    if(NOT BANJO_SWITCH_NEON_VMX_HELPERS)
        list(APPEND _banjo_round_definitions REX_VMX_HELPERS_X86_FORM)
    endif()
    if(BANJO_SWITCH_OMIT_FRAME_POINTER AND TARGET renut_recomp)
        target_compile_options(renut_recomp PRIVATE -fomit-frame-pointer)
        # The localized copies are game code too (sa2-nx: the same rule for that file).
        set_property(SOURCE ${BANJO_HOT_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -fomit-frame-pointer)
    endif()
endif()
foreach(_banjo_target renut_recomp renut)
    if(TARGET ${_banjo_target} AND _banjo_round_definitions)
        target_compile_definitions(${_banjo_target} PRIVATE ${_banjo_round_definitions})
    endif()
endforeach()
message(STATUS "banjo: sa2-nx's CPU rounds: definitions '${_banjo_round_definitions}'")

# banjo-nx (2026-10-21, the user's request): link-time and profile-guided optimisation of the Switch build, from
# sa2-nx (its app/CMakeLists.txt and docs/enhancements.md, "Link-time optimisation of a recompiled program"). Both OFF
# in the everyday build folder (app/out/switch): a release is built in folders of its own (docs/pgo-lto.md).
#   - BANJO_SWITCH_LTO: the recompiled game (renut_recomp), the app (renut), Renut's renderer (banjo_renut_gpu) and
#     plume are compiled to GIMPLE (-flto -fno-fat-lto-objects) and optimised as one program at the link (-flto=N
#     -flto-partition=balanced: N parallel LTRANS jobs, low for the build machine's memory). The SDK libraries and uam
#     stay out. The link repeats -ffp-contract=off and -fno-strict-aliasing (the LTRANS stage must not fuse what the
#     compile stage kept apart), and xxHash is built with XXH_FORCE_MEMORY_ACCESS=0: nfsmw-nx's LTO+PGO builds hit
#     xxHash's default (a strict-aliasing violation) once it was inlined into a cache-key function (garbage keys).
#   - BANJO_PGO=generate: an instrumented NRO that streams its branch counts to pgo/ next to the NRO
#     (src/banjo_pgo.cpp; registered in a section, not by constructors: libgcov's exit-time files do not fit the
#     Switch). Value profiling is off: its shared lists are not safe without atomic updates.
#   - BANJO_PGO=use: the counts merged on the PC (tools/pgo_merge.py) from BANJO_PGO_DIR (pgo/ at the root): one
#     .gcda per object, named by the object's path in the build folder with # for each separator
#     (-fprofile-prefix-path), so the instrumented and the optimised builds may use different build folders. Code the
#     sessions did not run is compiled as without a profile (-fprofile-partial-training); functions changed since the
#     instrumented build keep no profile (-Wno-coverage-mismatch). GCC instruments before its interprocedural inliner,
#     so the instrumented build needs no LTO: only the sources and the compile options must match.
#   - With a profile, the game code's inliner limits (sa2-nx's measurements): max-inline-insns-auto 64 (the
#     compiler's 30 refused call sites that ran billions of times) and inline-unit-growth 12 (without it the higher
#     limit fills the default 40 per cent with code the sessions never ran). Compile options that do not touch a
#     recorded profile.
if(REXGLUE_PLATFORM_SWITCH)
    option(BANJO_SWITCH_LTO "Link-time optimisation of the game code, the app and Renut's renderer (Switch)" OFF)
    set(BANJO_SWITCH_LTO_JOBS "4" CACHE STRING "Parallel LTRANS jobs of the Switch LTO link")
    option(BANJO_SWITCH_IPA_PTA "-fipa-pta at the Switch LTO link (sa2-nx and UnleashedRecomp-NX use it)" OFF)
    set(BANJO_SWITCH_INLINE_AUTO "64" CACHE STRING
        "max-inline-insns-auto of the game code with a profile (empty: the compiler's)")
    set(BANJO_SWITCH_INLINE_UNIT_GROWTH "12" CACHE STRING
        "inline-unit-growth of the game code with a profile (empty: the compiler's)")
    set(BANJO_PGO "" CACHE STRING "Profile-guided optimisation of the Switch build: empty, generate or use")
    set_property(CACHE BANJO_PGO PROPERTY STRINGS "" generate use)
    set(BANJO_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Folder of the PGO profile (.gcda files)")
    set(_banjo_opt_targets renut_recomp renut banjo_renut_gpu plume)
    if(BANJO_SWITCH_LTO)
        set(_banjo_lto_compile -flto -fno-fat-lto-objects)
        set(_banjo_lto_link -flto=${BANJO_SWITCH_LTO_JOBS} -flto-partition=balanced -ffp-contract=off
                            -fno-strict-aliasing)
        if(BANJO_SWITCH_IPA_PTA)
            list(APPEND _banjo_lto_compile -fipa-pta)
            list(APPEND _banjo_lto_link -fipa-pta)
        endif()
        foreach(target ${_banjo_opt_targets})
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE ${_banjo_lto_compile})
                target_compile_definitions(${target} PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
            endif()
        endforeach()
        target_link_options(renut PRIVATE ${_banjo_lto_link})
        message(STATUS "banjo: Switch LTO (${BANJO_SWITCH_LTO_JOBS} jobs)")
    endif()
    get_filename_component(_banjo_pgo_dir "${BANJO_PGO_DIR}" ABSOLUTE)
    file(TO_NATIVE_PATH "${_banjo_pgo_dir}" _banjo_pgo_dir)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" _banjo_pgo_prefix)
    if(BANJO_PGO STREQUAL "generate")
        set(_banjo_pgo "-fprofile-generate=${_banjo_pgo_dir}" "-fprofile-prefix-path=${_banjo_pgo_prefix}"
                       -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(renut PRIVATE BANJO_PGO_GENERATE=1)
    elseif(BANJO_PGO STREQUAL "use")
        if(NOT EXISTS "${BANJO_PGO_DIR}")
            message(FATAL_ERROR "BANJO_PGO=use: no profile in ${BANJO_PGO_DIR}")
        endif()
        # -fprofile-correction: the counts were taken without atomic updates (-fprofile-update=single) while several
        # threads ran the same code, so a few increments were lost and some blocks' counts do not add up; GCC
        # smooths them instead of stopping.
        set(_banjo_pgo "-fprofile-use=${_banjo_pgo_dir}" "-fprofile-prefix-path=${_banjo_pgo_prefix}"
                       -fprofile-correction -fno-profile-values -fprofile-partial-training -Wno-missing-profile
                       -Wno-coverage-mismatch)
        set(_banjo_inline_options "")
        if(BANJO_SWITCH_INLINE_AUTO)
            list(APPEND _banjo_inline_options --param=max-inline-insns-auto=${BANJO_SWITCH_INLINE_AUTO})
        endif()
        if(BANJO_SWITCH_INLINE_UNIT_GROWTH)
            list(APPEND _banjo_inline_options --param=inline-unit-growth=${BANJO_SWITCH_INLINE_UNIT_GROWTH})
        endif()
        if(_banjo_inline_options AND TARGET renut_recomp)
            target_compile_options(renut_recomp PRIVATE ${_banjo_inline_options})
            set_property(SOURCE ${BANJO_HOT_SOURCES} APPEND PROPERTY COMPILE_OPTIONS ${_banjo_inline_options})
        endif()
    elseif(BANJO_PGO)
        message(FATAL_ERROR "BANJO_PGO must be empty, generate or use (got ${BANJO_PGO})")
    endif()
    if(BANJO_PGO)
        foreach(target ${_banjo_opt_targets})
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE ${_banjo_pgo})
            endif()
        endforeach()
        target_link_options(renut PRIVATE ${_banjo_pgo})
        message(STATUS "banjo: PGO ${BANJO_PGO} (${BANJO_PGO_DIR})")
    endif()
endif()

# The app includes imgui.h and xxhash.h directly. An installed SDK package puts
# them in its single include folder; when the SDK is built from source
# (REXSDK_DIR) they are private to rexruntime, so add them here.
# SPIR-V initializers of the native renderer's EDRAM transfer / utility shaders (src/me/masseffect/shaders), compiled
# on the host with glslangValidator (tools/build_glslang.bat builds it from the SDK's glslang). For the Switch build
# the same host tool is used: SPIR-V is platform independent.
set(BANJO_GLSLANG_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/../out/glslang/StandAlone/glslangValidator.exe"
    CACHE FILEPATH "Host glslangValidator")
set(BANJO_GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
set(BANJO_EDRAM_SHADER_INCLUDES)
foreach(_shader masseffect_edram_7e3_to_rgba8 masseffect_edram_rgba8_to_7e3 masseffect_edram_16f_to_16f me_resolve_exp_bias me_edram_depth_to_rgba8 me_edram_depth_to_16f me_edram_import me_edram_color_to_depth me_edram_depth_to_depth me_edram_depth_to_stencil me_edram_color_to_stencil me_edram_depth_to_depth_msaa2 me_edram_depth_msaa2_to_depth_1x me_edram_depth_to_stencil_msaa2 me_edram_depth_msaa2_to_stencil_1x me_edram_raw64_to_rgba8 me_edram_rgba8_to_raw64 me_edram_raw64_to_16f me_edram_16f_to_raw64 me_edram_raw64_to_raw64 me_edram_depth_to_raw64 me_edram_raw64_to_depth me_depth_resolve_guestspace me_depth_resolve_guestspace_msaa2 me_edram_stencil_to_buffer me_edram_stencil_msaa2_to_buffer me_edram_color_stencil_to_buffer me_edram_color_to_color me_edram_r64_to_r64 sa2_resolve_copy)
    set(_extension comp)
    if(_shader STREQUAL "me_edram_import")
        set(_extension vert)
    elseif(_shader MATCHES "^me_edram_(color_to_depth|depth_to_depth|depth_to_stencil|color_to_stencil|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x|raw64_to_depth|color_to_color|r64_to_r64)$" OR _shader STREQUAL "sa2_resolve_copy")
        set(_extension frag)
    endif()
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/src/me/masseffect/shaders/${_shader}.${_extension}")
    set(_output "${BANJO_GENERATED_SHADER_DIR}/${_shader}.inc")
    set(_target_flags)
    if(_shader MATCHES "^me_edram_(stencil_to_buffer|color_stencil_to_buffer|stencil_msaa2_to_buffer|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x)$" OR _shader STREQUAL "me_depth_resolve_guestspace_msaa2")
        set(_target_flags --target-env vulkan1.2)
        set(_output "${BANJO_GENERATED_SHADER_DIR}/${_shader}_generated.inc")
    endif()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BANJO_GENERATED_SHADER_DIR}"
        COMMAND "${BANJO_GLSLANG_VALIDATOR}" -V ${_target_flags} -x -o "${_output}" "${_source}"
        DEPENDS "${_source}" VERBATIM)
    list(APPEND BANJO_EDRAM_SHADER_INCLUDES "${_output}")
endforeach()
add_custom_target(banjo_edram_shaders DEPENDS ${BANJO_EDRAM_SHADER_INCLUDES})
add_dependencies(renut banjo_edram_shaders)
target_include_directories(renut PRIVATE
    "${BANJO_GENERATED_SHADER_DIR}"
    src/me
    src/me/masseffect
    src/me/masseffect/shaders
    "${REXSDK_DIR}/src/core")

# CPU-only SDK sources the native renderer reuses (draw-extent estimation, shader microcode interpretation): the
# GPU plugin's private symbols cannot satisfy application-side use.
target_sources(renut PRIVATE
    "${REXSDK_DIR}/src/graphics/register_file.cpp"
    "${REXSDK_DIR}/src/graphics/format/ucode.cpp"
    "${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp")

# Precompiled SPIR-V of the SDK presenter (vulkan_spirv/guest_output_*): the native renderer draws the game
# image into the presenter output with it.
target_include_directories(renut PRIVATE "${REXSDK_DIR}/src/ui/shaders")

foreach(_dep imgui xxhash renderdoc)
    if(TARGET ${_dep})
        target_include_directories(renut PRIVATE
            $<TARGET_PROPERTY:${_dep},INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endforeach()

# Nintendo Switch: renut.nro next to the ELF. On the SD card it goes in
# sdmc:/switch/nights-nx/ (as nights-nx.nro) with the extracted package in game_root/ beside it.
if(REXGLUE_PLATFORM_SWITCH)
    # Link map: which object each function came from, for ranking the profiler's samples per function
    # (tools/switch-cpu-profile.py). It must come from the same build as the NRO that was profiled.
    target_link_options(renut PRIVATE "LINKER:-Map=${CMAKE_BINARY_DIR}/renut.map")
    # nights-nx (2026-10-29, the user's image): the homebrew menu's icon, a 256x256 JPEG.
    set(RENUT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
    set(RENUT_ICON_ARG)
    if(EXISTS "${RENUT_ICON}")
        set(RENUT_ICON_ARG ICON "${RENUT_ICON}")
    endif()
    # nights-nx (2026-10-29, from sa2-nx): the release settings, compiled in (src/renut_app.h BuiltInConfig): what
    # renut.toml no longer lists. The SD card's renut.toml holds the player's options and overrides these key by key.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/nights_switch_defaults.toml" NIGHTS_BUILTIN_CONFIG)
    # And the player's renut.toml, written next to the NRO when it is missing (src/renut_app.h DefaultConfigFile).
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/nights_switch.toml" NIGHTS_PLAYER_CONFIG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/nights_switch.toml"
                 "${CMAKE_CURRENT_SOURCE_DIR}/nights_switch_defaults.toml")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/nights_builtin_config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/builtin_config/nights_builtin_config.h" @ONLY)
    target_include_directories(renut PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/builtin_config")
    target_compile_definitions(renut PRIVATE NIGHTS_BUILTIN_CONFIG=1)
    rexglue_switch_add_nro(renut
        NAME "NiGHTS into Dreams"  # no "&": cmd.exe would split the post-build command
        AUTHOR "ChanseyIsTheBest"
        VERSION "1.0.1"
        ${RENUT_ICON_ARG})
endif()

# PC: line tables (CodeView) and a PDB, so the sampling profiler's addresses (nfsmw_perfil_pc) and crashes can be
# named with llvm-symbolizer. Debug info only: the generated code is the same.
if(WIN32)
    target_compile_options(renut PRIVATE -gline-tables-only -gcodeview)
    target_link_options(renut PRIVATE "LINKER:/DEBUG:FULL")
endif()

if(REXGLUE_PLATFORM_SWITCH AND BANJO_SWITCH_RENDERER_LINES)
    set_source_files_properties(src/me/me_native_system.cpp src/me/masseffect/masseffect_native_draws.cpp
        src/me/masseffect/masseffect_native_targets.cpp PROPERTIES COMPILE_OPTIONS "-g1")
endif()
