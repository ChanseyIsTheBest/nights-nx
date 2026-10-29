// nights-nx (tools/make_dropped_cvars.py, generated): the settings of banjo-nx's N&B-only sources
// (app/CMakeLists.txt NIGHTS_NB_SOURCES, not built for NiGHTS) that the shared code still declares,
// defined here as they were there. Their features are not in this build; the values only keep tomls
// and the enhancement framework's report working.

#include <cstdint>
#include <string>

#include <rex/cvar.h>

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(banjo_background_compile_core, 2, "Banjo",
                     "Native renderer: the background compile threads (pipeline prewarm, banjo_gpu_bool_spec's "
                     "background compiles, the shader statistics copies, the shader preload) are pinned to this core, "
                     "so together they never take more than one core from the game (their priority cannot go below "
                     "the game threads' 0x3B on the console). -1 = not pinned (any core, as before)")
    .range(-1, 2)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\banjo_d3d_clear.h
REXCVAR_DEFINE_BOOL(banjo_d3d_api_clear, true, "Banjo/Enhancements",
                    "Direct3D's Clear as an operation (GPU enhancement): a marker before and after Direct3D's clear "
                    "packets tells the renderer that the rectangles are a Clear of the whole surface, so the surface's "
                    "partly covered edge tiles (pitch padding, the rest of the last tile row) are taken whole instead of "
                    "being copied in from the EDRAM's previous owner. In-run A/B target 20")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\banjo_d3d_clear.h
REXCVAR_DEFINE_BOOL(banjo_d3d_api_resolve, true, "Banjo/Enhancements",
                    "Direct3D's Resolve as an operation (GPU enhancement, step 2 of the API layer): a marker around "
                    "Direct3D's resolve packets gives the renderer the resolved surface's size when the resolve covers "
                    "it whole, so the clear after the resolve takes the surface's edge tiles whole (pitch padding, the "
                    "rest of the last tile row) like the Clear operation, instead of copying them in from their previous "
                    "owner. In-run A/B target 21")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_d3d_host_submit, true, "Banjo",
                    "Native renderer (CPU enhancement): Direct3D's kick hands the segments it submits to the decoder "
                    "through a host queue (address, length, ring position): the decoder no longer parses the primary "
                    "ring (only what the kick did not write, as before). Self-checked against the ring's words")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\banjo_d3d_native.h
REXCVAR_DEFINE_BOOL(banjo_d3d_native_draw_indexed, true, "Banjo/Enhancements",
                    "Native Direct3D draw path (CPU enhancement): D3DDevice_DrawIndexedVertices with its draw-time "
                    "state flush as native code that writes the same PM4 into Direct3D's command buffer. Checked "
                    "against the original on a device copy (first calls of each thread, then 1 in "
                    "banjo_hot_guard_period; every call with banjo_cpu_verify)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\banjo_d3d_native.h
REXCVAR_DEFINE_BOOL(banjo_d3d_native_draw_vertices, true, "Banjo/Enhancements",
                    "Native Direct3D draw path (CPU enhancement): the same for D3DDevice_DrawVertices")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_d3d_records, true, "Banjo",
                    "Native renderer (CPU enhancement): every Direct3D call that writes into a command buffer (the draw "
                    "functions, Swap, Resolve, Clear, the tile functions, fences: hooks of tools/d3d/d3d_record_hooks.py) "
                    "has all its packets made into host records the decoder applies; only indirect buffers and "
                    "WAIT_REG_MEM stay PM4 unless banjo_d3d_records_barriers carries them too. Self-checked at the call and by the "
                    "decoder. "
                    "Replaces banjo_direct_state's draw-state records when both are on")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_d3d_records_barriers, true, "Banjo",
                    "Native renderer (CPU enhancement, with banjo_d3d_records; step 6b): the records carry the indirect "
                    "buffers and WAIT_REG_MEM too, and the decoder applies them from the records, so no PM4 is left in "
                    "Direct3D's segments")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_d3d_records_native, true, "Banjo",
                    "Native renderer (CPU enhancement, with banjo_d3d_records): the native Direct3D draw path writes "
                    "its records' operations itself as it writes its packets (no decoding of them on the drawing "
                    "threads); what Direct3D's own helpers write in between is decoded. Self-checked against the packets")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_d3d_records_static, true, "Banjo",
                    "Native renderer (CPU enhancement, with banjo_d3d_records): a buffer without records (Direct3D's "
                    "fixed buffers: per-tile set-up and clears, run before every recorded buffer) that runs twice with "
                    "the same words becomes a record of its own: later runs compare its words with the kept copy and "
                    "apply the operations instead of parsing it")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_deferred_recording.cpp
REXCVAR_DEFINE_BOOL(banjo_deferred_packets, true, "Banjo",
                    "masseffect_native_deferred_recording: the draw path's per-draw commands are queued as packed "
                    "records in one slot (many draws per slot) instead of one slot per Vulkan call. Same commands in "
                    "the same order; false = one slot per call, as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_direct_state, false, "Banjo",
                    "Native renderer (CPU enhancement): Direct3D's per-draw state packets become a host record the "
                    "ring applies with the same register functions at the same place in the stream (one NOP packet "
                    "instead of ~14 to parse; tile replays reuse the record; self-checked). Off until measured on the "
                    "console")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_blend_skip, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): transparent pixels whose blend would leave the RGBA8 target "
                    "unchanged are discarded before the blend (no depth/stencil write, op ADD, destination factor 1 "
                    "once the source is 0; a NaN output is never discarded)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_bone_padding, true, "Banjo",
                    "GPU (non-CPU enhancement): every indexed vertex fetch buffer (bone palettes, the water's grid) is "
                    "uploaded with zeros after it, so an indexed fetch is a clamp and a load without a compare and a "
                    "select per word (48 selects a vertex in the 4-bone skinning shaders). A draw whose buffer is not "
                    "one of these uploads (a memory export, a copy found by the dedupe) keeps the old form. false = "
                    "SPEC2_BONES_SELECT. In-run A/B target 52")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_bool_spec, true, "Banjo",
                    "GPU (non-CPU enhancement): a pipeline is created with the values of the bool constants its shaders "
                    "test (specialization constants 1000 + N, BANJO_BOOL in shader_common.h), so the code behind a "
                    "bool that is off goes and the ones that are on lose their branch. Nuts & Bolts' material shaders "
                    "test 14 (twelve lights, two output paths): PS536 1,980 SPIR-V instructions unspecialised, 879 "
                    "with every bool off. The values are in the pipeline key (PipelineKey::bools). Exact: the value is "
                    "the one the shader reads at run time (verify compares). A new combination draws with the "
                    "generic pipeline while its own compiles (banjo_gpu_bool_spec_async). Needs a shader package from "
                    "2026-10-15 on. In-run A/B target 31")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_bool_spec_direct, true, "Banjo",
                    "GPU (non-CPU enhancement), banjo_gpu_bool_spec: the generic pipeline stands in only when it costs "
                    "no compile (already created, or in the cache). Otherwise the ring creates the specialised one at "
                    "once: a cache lookup when it is in the prewarm list, else one compile of the smaller variant "
                    "instead of two. The prewarm also skips the generic twins of specialised records. Exact: the "
                    "specialised and generic pipelines draw the same pixels; only which one is compiled changes")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_branchless_select, true, "Banjo",
                    "GPU (non-CPU enhancement): the translator's conditional values (predicate set / push / pop / "
                    "restore, kill tests, early alpha copies) are select() / and() instead of HLSL 2021's short-circuit "
                    "?: and &&, which DXC writes as a branch each and NAK keeps (it flattens nothing): 26 of the heavy "
                    "material shader's 73 branches. Same values: the operands have no side effects. false = the old "
                    "forms (SPEC_CONSTANT_SHORT_CIRCUIT, bit 3); a package before 2026-10-15 has only those. In-run "
                    "A/B target 30")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_clamp_cbuf, true, "Banjo",
                    "GPU (non-CPU enhancement): the Xenos RCP / RSQ / LOG clamps read -FLT_MAX / FLT_MAX from two words "
                    "of the shared block (a free constant-buffer operand of NAK's min / max on Maxwell) instead of two "
                    "32-bit immediates moved into registers before every use (12-27 moves in each heavy pixel shader). "
                    "The same values. false = SPEC2_CLAMP_IMMEDIATE. In-run A/B target 49")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_dead_raster, true, "Banjo",
                    "Native renderer (non-CPU enhancement): a small point-list draw outside the tile passes whose "
                    "site has always been overwritten whole by the next draw that touches a render target runs with "
                    "its target writes off (its vertex shader's memory writes stay): Banjo's particle simulation "
                    "draws before the scene imported 832 EDRAM tiles a frame for nothing. Checked on every use")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_depth1x_zcull, false, "Banjo",
                    "Native renderer (non-CPU enhancement): 1x depth views are created without TRANSFER_DST, so the "
                    "driver can give them a ZCULL plane (hierarchical depth culling: it rejects only what the depth "
                    "test rejects). Their stencil imports take the 3D masked passes. No in-run A/B (image creation); "
                    "configs/banjo_depth1x_zcull.toml turns it on. Check the shadows on the console")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_early_alpha, true, "Banjo",
                    "GPU (non-CPU enhancement): an alpha-tested pixel shader that has an early alpha copy "
                    "(tools/early_alpha.py, SPEC_CONSTANT_EARLY_ALPHA) tests its alpha right after the last instruction "
                    "the alpha needs, and a 2x2 quad whose four pixels all fail there stops before the rest of the "
                    "shader instead of at its end. Exact: the copy is the same operations on the same values (checked "
                    "against the original by the tool), a pixel fails early only when the test fails for every alpha "
                    "within (|alpha| + |ref|) * 2^-12 of it, and no shaded pixel loses a derivative neighbour. In-run "
                    "A/B target 26")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_alias_by_shape, true, "Banjo",
                    "Native renderer (non-CPU enhancement): a redirected depth clear drawn through a 4x view finds the "
                    "1x view of the same memory by its shape (same base, format and tile pitch) when none has been "
                    "learned from a transfer. Nuts & Bolts' first shadow map: its clear view is the raster-grid kind, "
                    "which the learning never matched, so the clear was drawn and 768 tiles converted every frame")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_clear_color0, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx), EDRAM mode 4: a depth + color 0 clear drawn through a 4x alias "
                    "(Direct3D's Clear) is cleared in the 1x owners of both instead of drawn and transferred 4x -> 1x "
                    "(same bytes)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_clear_merge, true, "Banjo",
                    "Native renderer (non-CPU enhancement): a redirected depth clear (masseffect_native_edram4_"
                    "redirected_clear) records one full barrier before and one after all of its clears, and one LOAD "
                    "pass per owner image clearing all of that owner's rectangles with one vkCmdClearAttachments "
                    "(rows of tiles that line up merged into one rectangle), instead of a barrier, a CLEAR pass and a "
                    "barrier per run of tiles (393 barriers and ~180 passes per frame in the level on the console). "
                    "Same pixels with the same values: the owners are distinct images or disjoint rectangles, and a "
                    "clear that needs work first (an owner to prepare or restore, a partial stencil to fetch) takes "
                    "the old path. In-run A/B target 28")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_color_consumer, true, "Banjo",
                    "Native renderer (non-CPU enhancement): the colour half of a redirected depth + colour Clear goes "
                    "straight into the view that pulled its tiles the last two times (as the depth half already "
                    "does), which then owns them: the scene's first draw moves no colour tile (768 a frame from the "
                    "1x views into the scene's view before)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_copy_batch, true, "Banjo",
                    "Native renderer (non-CPU enhancement): consecutive tile copies between the same two views of one "
                    "synchronization are one vkCmdCopyImage with all their regions (48 separate copies, each between "
                    "its own pair of barriers, for the second shadow map's edge column every frame)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_lean_barriers, true, "Banjo",
                    "Native renderer (non-CPU enhancement): no full barrier before and after the EDRAM clear passes "
                    "(redirected and resolve clears, the tiles-once clear, colour-0 halves, stencil clears). Their "
                    "render pass orders them already: its begin waits for all earlier commands and makes earlier "
                    "attachment and copy writes visible, its end makes the clear visible to shaders, copies and later "
                    "passes; a pending batch of compute transfers gets its own barrier first (compute writes are not in "
                    "the pass's scope). The same clears, values and order of access. In-run A/B target 33")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram4_split_tiles, true, "Banjo",
                    "Native renderer (non-CPU enhancement), EDRAM mode 4: a draw that overwrites part of a tile another "
                    "view holds, and touches nothing else of it (the edge strip of Direct3D's Clear), takes the tile "
                    "without a transfer; the old owner keeps the rest, which is brought over only if something needs "
                    "it before the tile is overwritten whole. The two shadow maps' edge columns were 100 tiles a frame")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_edram_checks_cpu, true, "Banjo",
                    "Native renderer (non-CPU enhancement): the multisample EDRAM import shaders' image checks (sample "
                    "counts and sizes, four texture queries a pixel a pass) are made once on the CPU and the "
                    "pipelines are created without them (specialization constant kCheckImages). An import whose "
                    "check fails is refused, which is what the shaders' discard of every pixel amounted to. In-run "
                    "A/B target 54")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_identity_remap, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): a pipeline whose vertex inputs all keep their component order "
                    "skips the per-input remap test in its vertex shader")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_index_base_held, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): vertex shaders the translator proved never read the vertex "
                    "index run with r0 starting at 0 and the per-draw vertex index base is held at 0, so draws keep the "
                    "same shared constant block")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_literals_cbuf, true, "Banjo",
                    "GPU (non-CPU enhancement, package 26): a shader's literal constants that NAK cannot encode as "
                    "immediates (any of the low 12 mantissa bits set) are read from the constant registers Direct3D "
                    "loaded them into: a free constant-buffer operand instead of a move before each use (107-117 moves "
                    "in the in-place material shaders: their shadow taps' Poisson offsets). The renderer compares "
                    "those registers with the definitions at every draw and takes the old form "
                    "(SPEC2_LITERALS_IMMEDIATE) for a draw where one differs. In-run A/B target 56")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_no_texture_signs, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): when no texture of a draw (pixel and vertex shader slots) has a "
                    "biased or gamma component, its shaders skip the per-fetch sign test (a 'signed' component, sign 1, "
                    "is fetched as sampled by the sign code too, so it counts as none from 2026-10-15)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_nvk_draw_contract, true, "Banjo",
                    "GPU / driver (non-CPU enhancement, sa2-nx): NVK's per-draw contract is also looked up under the "
                    "name the linked Mesa tree exports (nvk_switch_dibujo), so its per-draw paths (draw emission in one "
                    "go, only dirty cbuf slots and dynamic state groups, prefetch hints; each self-checked in NVK and "
                    "switched off by its guard on a difference) are requested. false = only the name nvk_switch_draw, "
                    "as before (never found: nothing requested)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_pass_narrow_end, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): a render pass's end dependency names its consumers (shader, "
                    "transfer and attachment access) instead of MEMORY_READ | MEMORY_WRITE, so NVK does not invalidate "
                    "its constant, QMD and macro caches at every pass end")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_pred_select, true, "Banjo",
                    "GPU (non-CPU enhancement): a vertex shader's predicated blocks (if (p0) { ... }) that hold only "
                    "register arithmetic, static constants and indexed vertex fetches run for every vertex, each "
                    "register they write saved first and put back with select() where the predicate does not hold "
                    "(shader_recompiler.cpp banjoFlattenPredicated). NVK flattens an if only up to 8 ALU operations "
                    "and no load, so each bone of the 4-bone skinning shaders was a branch whose loads waited for the "
                    "previous bone's block: four memory latencies in a row per vertex, in small draws. Exact: the "
                    "statements have no side effect and cannot fault (the fetches are clamped into their buffer). "
                    "false = the old form (SPEC_CONSTANT_PRED_BRANCHES, bit 2); a package before 2026-10-16 has only "
                    "that. In-run A/B target 37")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_pred_select_ps, true, "Banjo",
                    "GPU (non-CPU enhancement): banjo_gpu_pred_select for pixel shaders, blocks of at most 12 "
                    "statements (a flattened block also runs for the warps a branch skipped whole, so only short "
                    "ones; no texture fetch, kill, output or derivative in them). The heavy material shaders keep "
                    "19-25 of their 37 branches (offline). Exact, as banjo_gpu_pred_select. false = the old form "
                    "(SPEC_CONSTANT_PRED_BRANCHES_PS, bit 4). In-run A/B target 38")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_proof_depth_only, true, "Banjo",
                    "Native renderer (non-CPU enhancement): in the depth-only EDRAM mode a draw's alpha test and "
                    "alpha-to-coverage bits do not stop the overwrite proof (no pixel shader runs there; the "
                    "renderer draws it without a fragment stage). Banjo's loading screen clears depth that way with "
                    "the alpha test left on: the whole depth buffer changed representation twice a frame")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_proof_tess_leftover, true, "Banjo",
                    "Native renderer (non-CPU enhancement): the overwrite proof of a Direct3D Clear treats "
                    "VGT_OUTPUT_PATH_CNTL as the reference renderer does, in force only in the explicit major mode. "
                    "The game leaves it on the tessellator after its water, and every clear after that was drawn as "
                    "an ordinary draw (EDRAM contents carried into it and out of it again)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_proof_triangle, true, "Banjo",
                    "Native renderer (non-CPU enhancement): one auto-indexed triangle that covers its whole viewport "
                    "or scissor (the game's post-processing and composite passes) is proven to overwrite what it "
                    "writes in full, like a rectangle: the tiles under it are not brought in from their previous "
                    "owner first")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_push_once, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): the push constants (three addresses the set-4 shaders never "
                    "read) are recorded once per pass / command buffer instead of on every draw whose constant "
                    "offsets moved")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_rect_split, true, "Banjo",
                    "Native renderer (non-CPU enhancement): an auto-indexed rectangle list of 2 to 8 rectangles (what "
                    "D3D's Clear draws for the part of its area outside whole EDRAM tiles) is drawn as one draw per "
                    "rectangle, so the tiles pulled into the drawn view are those under each rectangle and not all "
                    "of the box around them")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_resolve_clear_merge, true, "Banjo",
                    "Native renderer (non-CPU enhancement): a resolve's colour clear records all of its rectangles in "
                    "one LOAD pass with one vkCmdClearAttachments (banjo_gpu_edram4_clear_merge's form), and so does its "
                    "depth clear when the image has no ZCULL plane (TRANSFER_DST). Depth images with a plane keep one "
                    "loadOp = CLEAR pass per rectangle: NVK resets ZCULL only for those, and reloading a stale plane "
                    "can kill the GPU context (UnleashedRecomp's GPU loss). The same rectangles and values. In-run A/B "
                    "target 36")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_resolve_draw, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): same-format RGBA8 color resolves are drawn on the 3D engine "
                    "(one fragment pass that copies the texels) instead of vkCmdCopyImage on NVK's copy engine (no "
                    "engine switch and wait for idle per resolve)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_robustness_off, true, "Banjo",
                    "GPU (non-CPU enhancement): the game's pipelines are created with buffer and image robustness "
                    "disabled (VK_EXT_pipeline_robustness; vertex input as the device has it), so NVK drops its two "
                    "descriptor index clamps from every texture handle (nvk_nir_lower_descriptors.c, "
                    "clamp_desc_array_bounds). The renderer's indices are in range by construction. A pipeline key "
                    "bit (spec2 bit 8, no shader reads it) keeps the two forms apart. In-run A/B target 53")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_sign_mask_skip, true, "Banjo",
                    "GPU (non-CPU enhancement): a texture slot's word loses its packed sign 1 (\"signed\", which the "
                    "shaders fetch as sampled, like 0), so a slot without an effective sign has a top byte of 0 and "
                    "the shaders skip their `& 0x00FFFFFF` there. false = SPEC2_SIGN_MASK_ALWAYS and the words as "
                    "before. In-run A/B target 50")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_skip_dead_draws, true, "Banjo",
                    "Native renderer (non-CPU enhancement), EDRAM mode 4: a draw that writes no colour (depth-only mode, "
                    "or no target its pixel shader writes with a colour mask), no depth (depth write off), no stencil "
                    "(off, or both faces with write mask 0 or every op KEEP) and no memory (no memexport) is skipped "
                    "before its EDRAM preparation. Verify mode draws them and checks that the renderer's own write "
                    "analysis publishes nothing for them. In-run A/B target 22")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_spec_per_stage, true, "Banjo",
                    "GPU (non-CPU enhancement): each stage of a pipeline is given only the specialization constants "
                    "its SPIR-V reads (constant 0 reduced to the bits it tests, 60/61 and the bools only if declared), "
                    "so the driver's per-stage cache finds a shader it already compiled instead of compiling it "
                    "again for each pairing. A module whose use of constant 0 is not a plain bit test is given "
                    "everything, as before. Exact: every constant a module reads has the same value")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_stencil_clear_partial, true, "Banjo",
                    "Native renderer (non-CPU enhancement, package 26): a draw proven to replace the stencil of a "
                    "rectangle without reading it (Direct3D's Clear: test ALWAYS, REPLACE or ZERO, write mask 0xFF, "
                    "the rectangle covered whole) no longer fetches the deferred stencil of every tile of its view "
                    "first. Tiles inside the rectangle need none (the draw replaces all of it), tiles it does not "
                    "touch keep their deferred stencil (fetched by the first draw that reads them, as for any draw), "
                    "and only the tiles it covers in part are fetched, one clipped pass per strip of them. The once a "
                    "frame 96-tile fetch before the 96x96 Clear in the levels and the 512-tile one before the 512x512 "
                    "Clear of the screen after the title. In-run A/B target 57")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_stencil_fetch_3d, true, "Banjo",
                    "Native renderer (non-CPU enhancement): a late stencil fetch (masseffect_native_edram4_stencil_lazy) "
                    "takes the 3D engine's masked passes (a stencil clear of the tile rows, then one pass per bit ever "
                    "set in the source; none at all when no bit was ever set) instead of the copy engine. Same stencil "
                    "bytes: the masked passes are the original import path. In-run A/B target 32")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_structured_cf, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): shaders whose forward-only control flow the translator proved "
                    "equivalent as nested if/else run that form instead of an `if (skipTo <= pc)` test per CF "
                    "instruction")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_tess_grid, true, "Banjo",
                    "GPU (non-CPU enhancement): tessellated triangle patches (Nuts & Bolts' water: 33 patches at 16 "
                    "segments, 25,344 vertex invocations a draw, VS n414 1.8-2.9 ms a frame on the console) are drawn "
                    "indexed, one vertex invocation per grid point ((N + 1)(N + 2) / 2 a patch instead of 3 N^2; each "
                    "point is shared by up to six triangles). The index list, built once per patch count, level and "
                    "winding, gives each corner of the old triangle list the grid point it decodes to, in the same "
                    "order (same triangles, same corners, same first vertex). Exact: the domain of a point is "
                    "computed from the same (a, b) with the same operations (banjoTessDomain, bit 30 of the level "
                    "word). Needs a shader package from 2026-10-16 on. In-run A/B target 39")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_texcoords_unswapped, true, "Banjo",
                    "GPU (non-CPU enhancement, sa2-nx): vertex shaders skip the swapped-texcoord test of every "
                    "texcoord input (the native renderer never swaps them: g_SwappedTexcoords is always 0)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_texture_signs_per_slot, true, "Banjo",
                    "GPU (non-CPU enhancement): a draw with signed, biased or gamma textures keeps the per-fetch sign "
                    "and gamma code only in the fetches of the fetch constants whose texture has them; the pipeline's "
                    "specialization constant 61 folds every other fetch to the path it takes anyway (packed signs 0: "
                    "the texel as sampled). Exact: a fetch constant whose texture words all have packed signs 0 gives "
                    "0 at run time too. The heavy material shaders have ~30 fetches and one or two such textures. "
                    "Needs a shader package from 2026-10-15 on (older ones ignore constant 61). In-run A/B target 27")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_tiles_once, true, "Banjo",
                    "Native renderer (non-CPU enhancement): the scene's two predicated tile passes are drawn once. "
                    "The first pass draws every draw for both tiles (into the first tile's rows and the rows below "
                    "them in the same host images), copies both at each resolve, and the second pass's draws and "
                    "copies are dropped after being checked against what the first pass made for them. See "
                    "TilesOnce in me_native_system.cpp for the conditions and the one visible difference (effects "
                    "that sample the scene across the tile boundary)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_vfetch_block, true, "Banjo",
                    "GPU (non-CPU enhancement): a vertex shader with fetches indexed by a computed register (the bone "
                    "palettes of skinned meshes) tests each element's range once and loads its words side by side "
                    "with no branch per word: each word loaded at its index clamped into the buffer and replaced by 0 "
                    "when the index is outside it, one uniform test for an empty buffer. Before, a range test and a "
                    "branch around every 32-bit load (48 per vertex in the 4-bone skinning shaders, ~1,400 NAK "
                    "instructions and 16,500 static cycles). Exact: the same word, or 0, for every word. false = the "
                    "old form (SPEC_CONSTANT_VFETCH_WORDWISE, bit 7, set on those vertex shaders only); a package "
                    "before 2026-10-15 has only the old form. In-run A/B target 29")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_vs_outputs_unwritten, true, "Banjo",
                    "GPU (non-CPU enhancement): a vertex output the pipeline's pixel shader does not read (constant "
                    "60) is not written at all, instead of written as 0, so the driver drops its store and its "
                    "attribute (the 4-bone skinning shaders stored 16 zero attributes a vertex). false = "
                    "SPEC2_OUTPUTS_WRITE_ZERO. In-run A/B target 51")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_gpu_zcull_greater, false, "Banjo/Enhancements",
                    "Switch (non-CPU enhancement, console trial): ask the driver for ZCULL in the GREATER direction "
                    "(NVK_ZCULL=greater), the one Banjo's reversed-depth scene can use. Culling is conservative, so "
                    "the picture should be the same; false = the driver's default direction (less)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_mesa_disk_cache_off, true, "Banjo/Enhancements",
                    "Switch (non-CPU enhancement): turn off Mesa's own on-disk shader cache (sdmc:/.mesa). The "
                    "renderer's pipeline cache file (cache/masseffect_native_pipelines.bin) already keeps the compiled "
                    "pipelines, and Mesa's SD-card writes held a lock the ring thread waited on during compiles")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_native_mip_range, true, "Banjo",
                    "Renderer (correctness, non-CPU group): a sampler's range of mip levels follows the console's rule "
                    "(the reference renderer's GetSubresourcesFromFetchConstant): both ends limited by the texture's "
                    "size, level 0 only without a mip address, and from level 1 when the base address is null (the "
                    "texture loader already leaves that level unread). Not met in the scenes tested so far; false = "
                    "as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_native_partial_resolve_inside, true, "Banjo",
                    "Renderer (correctness, non-CPU group, sa2-nx): a resolve is a later rectangle of an existing "
                    "resolved texture only when it declares that texture's height (or the rows left below its advanced "
                    "base) and its rectangle lies inside it; a "
                    "whole texture allocated a few pages above a freed one gets its own texture instead of being "
                    "written into the old one")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_native_resolved_retire_idle, true, "Banjo",
                    "Renderer (non-CPU group, sa2-nx): a resolved texture nothing resolved to or read for "
                    "banjo_native_resolved_idle_frames presented frames is released (the game freed its render "
                    "target's texture long ago). Without it every address ever resolved to keeps its image for the "
                    "whole session and GPU memory fills up in long sessions")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\cpu\banjo_reverb_native.cpp
REXCVAR_DEFINE_BOOL(banjo_native_reverb, true, "Banjo/Audio",
                    "Run XAudio's reverb (stereo_room_t<float>::process, 0x826683D0) as native code "
                    "instead of the recompiled guest code (bit-identical; the recompiled one costs "
                    "about a quarter of a Switch core)");

// src\cpu\banjo_reverb_native.cpp
REXCVAR_DEFINE_BOOL(banjo_native_reverb_verify, false, "Banjo/Audio",
                    "Debug: run both the native and the recompiled reverb on every call, keep the "
                    "recompiled result and log any byte that differs");

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_native_stacked_baseless, true, "Banjo",
                    "Renderer (correctness, non-CPU group): a stacked texture (several 2D layers, sampled as 3D) "
                    "whose fetch constant starts at a mip level above 0 is built from that level, read from the mip "
                    "chain, as the console samples it. The game streams textures in from the small levels and points "
                    "base and mip address at the same memory until the largest level has arrived; reading that "
                    "memory as the largest level put the 64x64 level in a corner of each 128x128 layer and nothing in "
                    "the rest: Banjo's fur mask, hence fur only in rectangular patches. false = as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_native_tfetch_filters, true, "Banjo",
                    "Renderer (correctness, non-CPU group): a sampler takes the filters the shader's texture fetch "
                    "instruction asks for (mag / min / mip / anisotropy, each unless the instruction says 'from the "
                    "fetch constant'), as the console does. false = only the fetch constant's filters, as before: "
                    "textures a shader magnifies with its own bilinear filter then come out in hard-edged texels")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\cpu\banjo_xaudio_native.cpp
REXCVAR_DEFINE_BOOL(banjo_native_xaudio_src, true, "Banjo/Audio",
                    "Run XAudio's mono 16-bit linear sample-rate converter (SRC::LinearVector::ProcessBlock<short,1>, "
                    "0x8267E990) as native code instead of the recompiled guest code: the same NEON operations without "
                    "the emulated vector gathers (bit-identical)");

// src\cpu\banjo_xaudio_native.cpp
REXCVAR_DEFINE_BOOL(banjo_native_xaudio_src_verify, false, "Banjo/Audio",
                    "Debug: run both the native and the recompiled converter on every call, keep the recompiled one's "
                    "result and log any difference in the output, the state or r3 / f1");

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_no_draw_pairing, true, "Banjo",
                    "Native renderer (CPU enhancement): skip the Draw* record pairing, which never pairs a draw in "
                    "Banjo (the ring identifies the shaders by itself): no record per draw on the game thread, no "
                    "search per draw on the ring. Same shaders")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_note_shader_fast, true, "Banjo",
                    "Native renderer (CPU enhancement, sa2-nx): the draw threads' shader-object bookkeeping remembers "
                    "finished objects in one shared table (no thread-local lookup, 8x the slots, fewer mutex takes). "
                    "Same outcome")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(banjo_prewarm_threads, 1, "Banjo",
                     "Native renderer: threads that prewarm the pipeline list (masseffect_native_pipelines_prewarm), "
                     "all at the lowest priority. 1 = as before")
    .range(1, 3)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_records_zero_ahead, true, "Banjo",
                    "Native renderer (CPU enhancement, with banjo_d3d_records; from sa2-nx's sa2_api_ring_zero_ahead): "
                    "the ring of records' free space just past its head is made into cache lines of zeros (DC ZVA on "
                    "the Switch: no DRAM read before the records' stores, which the 8 MB ring, four times the L2, "
                    "otherwise costs on every line; a memset on the PC, so the PC tests cover which words are zeroed)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_repack_cached, true, "Banjo",
                    "Native renderer (CPU enhancement): k_10_11_11 / k_11_11_10 vertex attributes are converted in a "
                    "cached scratch copy and written to the upload buffer once, instead of converted in place in the "
                    "upload buffer (no CPU cache on the Switch: every word was read back from it). Same bytes")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_bound_plan_memo, true, "Banjo",
                    "Ring thread (CPU enhancement, sa2-nx): the draw's EDRAM bound-range plan (PlanEdramBoundRanges, a "
                    "pure function of its request) is reused when the request equals the previous draw's")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_constants_adaptive, true, "Banjo",
                    "Native renderer (CPU enhancement): banjo_ring_constants_dedupe stops comparing (and keeping its "
                    "shadow copy) for a shader stage whose last 1024 compares found fewer than 16 equal blocks, for "
                    "that stage's next 16384 uploads, then looks again. Same output: it only stops trying")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_constants_dedupe, true, "Banjo",
                    "Native renderer (CPU enhancement): shader constants whose bytes equal the block uploaded last "
                    "reuse it (no upload, no new constant-buffer offset, fewer set-4 rebinds). Same bytes")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_INT32(banjo_ring_core, 1, "Banjo",
                     "Native renderer: the ring thread is pinned to this core (its affinity mask reduced to it). -1 = "
                     "the process's whole mask, as before. In-run A/B target 41 switches between the pin and the "
                     "whole mask")
    .range(-1, 2)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_decoder, true, "Banjo",
                    "Native renderer (CPU enhancement): a decoder thread decodes the game's PM4 (packets, register writes, "
                    "shader identification, the tile passes' bookkeeping) and hands the ring thread, now the executor, "
                    "the draws in stream order with the register pages written since the item before; fences, memory "
                    "writes, interrupts and the read pointer stay the executor's, in stream order. Every 1024th draw "
                    "(every 16th with banjo_cpu_verify) the executor's register file is compared with the decoder's. "
                    "In-run A/B target 43 (the off half: the decoder waits for each item, as one thread did)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_edram_memo, true, "Banjo",
                    "Native renderer (CPU enhancement): a draw with the same render-target state as the previous draw "
                    "reuses its EDRAM preparation (only when the full path would do no work: same images, tiles owned "
                    "in the same epoch, nothing pending; self-checked against the full path)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_estimator_reason, true, "Banjo",
                    "Ring thread (CPU enhancement, sa2-nx): the draw-extent estimator keeps its reject reason as a "
                    "pointer to the literal instead of a std::string")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_identity_once, true, "Banjo",
                    "Native renderer (CPU enhancement): a draw asks once whether its vertex shader entry matches the "
                    "loaded program (four lookups before). Same answers")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_index_epoch, true, "Banjo",
                    "Native renderer (CPU enhancement): the index cache matches index ranges of any size by address "
                    "within a synchronization epoch, as it already did above 16 KB (no XXH3 of every draw's indices; "
                    "self-checked)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_index_persistent, true, "Banjo",
                    "Native renderer (CPU enhancement): a 16-bit index range keeps its converted copy across frames in "
                    "the persistent arena of banjo_ring_vertex_persistent, under the same proof as a vertex block (the "
                    "same synchronization interval, or its bytes fingerprinted again), instead of being converted and "
                    "copied into the upload buffer on every frame. The first 4096 arena uses (all of them with "
                    "banjo_cpu_verify) are converted again and compared. In-run A/B target 25")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_memexport_bounds, true, "Banjo",
                    "Native renderer (CPU enhancement): vertex streams that overlap no memexport mirror skip the walk "
                    "over every mirror (a sorted index of the mirrors answers it). Same redirections")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_pmull_fingerprint, true, "Banjo",
                    "Native renderer (CPU enhancement, ARM only): where masseffect_native_fingerprint_mode = 2 hashes with "
                    "the CRC32 pair, a 64-bit CRC folded with PMULL carry-less multiplies takes its place "
                    "(banjo_pmull_fingerprint.h): half the instructions per byte on the A57, the same guarantees "
                    "(every change under 64 bits seen, otherwise 2^-64). Checked against its bit-by-bit reference on "
                    "the first 256 calls and 1 in 65536 after (1 in 64 with banjo_cpu_verify); a difference switches it "
                    "off for the session. No in-run A/B (fingerprints are compared across draws): A/B by toml")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_registers_in_place, true, "Banjo",
                    "Native renderer (CPU enhancement): a proven rectangle list changes the two registers the proof "
                    "needs in the ring's register file and puts them back, instead of copying the whole register file "
                    "for every rectangle list. Same proof")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_sampler_fast, true, "Banjo",
                    "Native renderer (CPU enhancement): a hit of the per-register sampler cache compares the fetch "
                    "constant in line and writes 1/size from the entry instead of dividing on every draw. Same words")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_shader_load_memo, true, "Banjo",
                    "Native renderer (CPU enhancement): a shader program loaded again from the same address with the "
                    "same words takes its identification from a memo by address (one compare) instead of being copied "
                    "three times and hashed. The first 4096 uses, all in verify mode, are checked against the content "
                    "memo")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_target_memo, true, "Banjo",
                    "Native renderer (CPU enhancement, sa2-nx): render-target and depth-target lookups answered from "
                    "a 4-slot memo of their inputs (the maps never drop an entry: same inputs, same image)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_texture_invalidate_range, true, "Banjo",
                    "Native renderer (CPU enhancement): a resolve that changes the texture an address is sampled from "
                    "clears only the sampler cache entries whose texture memory (base level and mips, sized at 16 bytes "
                    "a texel on 32x32 tiles: never smaller than the real one) can overlap the resolved range, instead of "
                    "the whole cache. Verify mode prepares every entry the whole-cache rule would have cleared and "
                    "compares; a difference switches the rule off. In-run A/B target 24")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_vertex_containment, true, "Banjo",
                    "Native renderer (CPU enhancement, sasrt-nx): a draw's vertex window inside a larger copy of the "
                    "same vertex buffer already in this upload buffer reuses it instead of copying again (large ranges, "
                    "the dedupe's rules; the first 4096 reuses, all in verify mode, checked against guest memory)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_vertex_epoch, true, "Banjo",
                    "Native renderer (CPU enhancement): within a synchronization interval a vertex window of any size "
                    "is matched to its earlier copy by address (the rule the windows above 16 KB, their containment "
                    "and the index cache already use), without a sampled fingerprint on every draw; containment for "
                    "every size too. Not for ranges in the command buffer pages. The first 4096 small reuses, all in "
                    "verify mode, compared with guest memory")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_vertex_lock_rule, false, "Banjo",
                    "Native renderer (CPU enhancement): a persistent vertex block (banjo_ring_vertex_persistent) whose "
                    "memory pages no Direct3D vertex or index buffer Lock has touched since its last fingerprint keeps "
                    "that fingerprint without reading its bytes again. 1 in 64 such uses still reads and compares (every "
                    "one with banjo_cpu_verify); a difference switches the rule off for the session. In-run A/B "
                    "target 23")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_vertex_persistent, true, "Banjo",
                    "Native renderer (CPU enhancement): a vertex window whose bytes were the same in three frames keeps "
                    "its converted copy in a persistent buffer; each later use fingerprints all its guest bytes once per "
                    "synchronization interval instead of copying them (the first 4096 uses, all in verify mode, compared "
                    "byte by byte)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_vertex_proven_first, true, "Banjo",
                    "Native renderer (CPU enhancement): a vertex window already proven in the persistent arena for "
                    "this synchronization interval is bound from it before the upload buffer's reuse tables are "
                    "searched (two lookups that cannot find it). Same binding")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_vs_hash_given, true, "Banjo",
                    "Native renderer (CPU enhancement, step 7 of docs/d3d-full-replacement.md): the vertex input cache's "
                    "key takes the XXH3 of the vertex program the ring computed when the program was loaded instead of "
                    "hashing the whole program again on every draw. The first 4096 keys (all with banjo_cpu_verify) "
                    "hash it again and compare. In-run A/B target 44")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(banjo_ring_wait_vblank, true, "Banjo",
                    "Native renderer (CPU enhancement): a long WAIT_REG_MEM sleeps until the next vblank interrupt or "
                    "submission (or 250 us) instead of polling every 50 us. Same results, fewer wake-ups")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(banjo_vertices_base_zero_r0, true, "Banjo",
                    "Ring thread: draws whose vertex shader reads r0 also take the base-zero vertex binding (one "
                    "upload-buffer binding per command buffer instead of one per draw), the vertex index base "
                    "compensated so r0 is unchanged")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_ring_wait.cpp
REXCVAR_DEFINE_BOOL(banjo_wait_blocking_ring, true, "Banjo",
                    "Native renderer: the game's D3D GPU waits (sub_8264DCD0 and its six loops) sleep until the ring "
                    "thread advances instead of spinning. Same values seen by the game, less CPU")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_deferred_recording.cpp
REXCVAR_DEFINE_BOOL(masseffect_deferred_native_fast, false, "Mass Effect",
                    "Queue: the producer publishes commands in batches (masseffect_deferred_native_batch) instead "
                    "of one seq_cst store + wake check per command, keeps its own copy of the worker's read index "
                    "and the worker stores it every 16 commands (less cache line ping-pong between the cores). "
                    "Same commands, same order; false = as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_deferred_recording.cpp
REXCVAR_DEFINE_BOOL(masseffect_deferred_native_update, false, "Mass Effect",
                    "vkUpdateDescriptorSets is queued too (its write arrays deep-copied into the arena) instead of "
                    "draining the worker (~70 drains per frame, each a spin-yield of the ring thread). The update runs "
                    "on the worker at the same position of the command stream; every submit, destroy and reset still "
                    "drains first")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_targets.cpp
REXCVAR_DEFINE_BOOL(masseffect_gpu_marks_categories, true, "Mass Effect",
                    "Write a GPU timestamp at every category change (per-category GPU report). false: only the "
                    "first and last timestamp per submission (no per-category breakdown, fewer GPU flushes)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_compare_fast, false, "Mass Effect",
                    "Per-draw state caches (sampler/fetch constants, pass key, shared block, push constants, viewport, "
                    "pipeline keys) compare their 8-130 byte keys with an inline NEON/word loop instead of a libc memcmp "
                    "call (~15 per draw); the EDRAM trace-tile query returns at once when no tile is traced. Same answers")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(masseffect_native_dedupe_max_fingerprint, 0, "Mass Effect",
                     "Same-frame vertex dedupe: ranges larger than this many bytes are matched by address, size and "
                     "byte order only, without the content fingerprint (static meshes; the transient UI/movie buffers "
                     "that need the check are small). 0 = fingerprint every range")
    .range(0, 1 << 24)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_deferred_recording.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_deferred_recording, false, "Mass Effect",
                    "Ring thread queues every vkCmd* call and a worker thread records them into the same command "
                    "buffers in the same order (moves NVK recording off the ring thread; same commands, same image)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_diag_reuse, true, "MASSEFFECT",
                    "Native renderer (measurement only): counts how many new textures have the "
                    "same content and shape as another live one in the cache (the game reloads packs at another address) "
                    "and how many of those other ones have gone unused for more than 120 frames. Changes nothing. false = no "
                    "counting")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_fast_copy, false, "Mass Effect",
                    "Vertex copy into the upload buffer: 64 bytes per iteration with the guest source prefetched ahead "
                    "(the guest data is cold: written frames ago by another core). Same bytes; checked against the plain "
                    "loop for the first masseffect_native_fast_copy_verify copies")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(masseffect_native_fast_copy_verify, 256, "Mass Effect",
                     "Self-check of masseffect_native_fast_copy: copies compared with the plain loop (0 = none)")
    .range(0, 2147483647)  // banjo-nx: banjo_cpu_verify checks every copy
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_fast_pair, false, "Mass Effect",
                    "Ring: draw/record pairing without the cold cache lines and locks: record start addresses in a "
                    "compact array (the best record of a packet is found in two 32-byte rows), object -> library entry "
                    "through a lock-free table, no draw-queue mutex per unpaired draw, prefetch while draining")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(masseffect_native_fingerprint_mode, 0, "Mass Effect",
                     "Content fingerprint of the sampled vertex/index/texture blocks (SampleFingerprint, index cache): "
                     "0 = XXH3 seeded per block (every seeded call over 240 bytes first regenerates XXH3's 192-byte "
                     "secret), 1 = unseeded XXH3 per block chained with a bijective mix (same 64-bit strength, no secret "
                     "setup), 2 = two-polynomial hardware CRC32 pair (masseffect_crc_fingerprint.h; same 2^-64 argument, needs "
                     "+crc, otherwise mode 1)")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_flat_identity, false, "Mass Effect",
                    "Ring: shader identity checks (VS fetch-patch match, PS microcode match) memoized in small "
                    "direct-mapped tables keyed by (library entry, load generation) instead of an unordered_map and a "
                    "word-by-word compare per use; exact (pure function of the loaded microcode and the entry)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_indices_cache, false, "Mass Effect",
                    "Same-frame index buffer cache (16-bit fast path): a draw that repeats the guest index range "
                    "(address, count, byte order) of an earlier draw in this upload buffer reuses its uploaded copy "
                    "and its min/max instead of converting and copying again (the x5 per-light re-draws). Ranges of "
                    "up to 16 KB are also fingerprinted (transient UI buffers)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_cbufs, true, "MASSEFFECT",
                    "Native renderer: NVK only looks at the cbuf slots that can get dirty "
                    "and skips the cbuf flush when nothing is dirty (self-checked in NVK). false = as usual");

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_dynamic, true, "MASSEFFECT",
                    "Native renderer: NVK only emits the dynamic state groups with "
                    "dirty bits (self-checked in NVK). false = as usual");

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_emission, true, "MASSEFFECT",
                    "Native renderer: NVK writes each draw command in one go, with the "
                    "same bytes (self-checked in NVK). false = as usual");

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(masseffect_native_nvk_measure, 64, "MASSEFFECT",
                     "Native renderer: measures by parts inside NVK on 1 in N calls "
                     "(power of 2; 'C6 NVK parts' report every 10 s). 0 = no measuring")
    .range(0, 4096);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_nvk_preload, true, "MASSEFFECT",
                    "Native renderer: cache hints (PRFM) for the pipeline and shaders before "
                    "they are used, and the pipeline handed to NVK after PipelineFor. Changes nothing visible. false = no hints");

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_pipelines_prewarm, true, "MASSEFFECT",
                    "Native renderer: at start-up, a lowest-priority thread re-creates "
                    "in the Vulkan cache the pipelines the ring created in earlier sessions "
                    "(its list is in cache/masseffect_native_pipelines.bin), with the same function as the ring, and destroys them: when the "
                    "ring requests them they are already compiled (no 70-160 ms hitches per pipeline on the first "
                    "run after a change). Changes no pipeline and no draw. false = no prewarming (the "
                    "list is still saved)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_pm4_fast, false, "Mass Effect",
                    "Ring: runs of constant/fetch register writes (type 0, SET_CONSTANT2/SET_SHADER_CONSTANTS, "
                    "LOAD_ALU_CONSTANT) are byte-swapped, compared and stored four words at a time with NEON "
                    "straight from the guest words; same register contents and generation bumps")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_raw_microcode, false, "Mass Effect",
                    "Ring: IM_LOAD of the same address and size as the last load of that stage compares the guest "
                    "words against a raw copy of the last load (one memcmp) instead of byte-swapping them first; "
                    "exact (the swap is a bijection)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_set4_differences, true, "MASSEFFECT",
                    "Native renderer: descriptor set 4 (UBO constants) by differences in "
                    "NVK: only the descriptors that change are sent and rebound. Same result (self-checked "
                    "in NVK). Needs the Mesa build with patch_nvk_set4; false = as usual");

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_INT32(masseffect_native_upload_memory, 0, "MASSEFFECT",
                     "Native renderer: upload buffer memory. 0 = the one the SDK picks (on the Switch, without "
                     "CPU cache), 1 = with CPU cache (published with vkFlushMappedMemoryRanges before "
                     "submitting), 2 = without CPU cache")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\me_native_system.cpp
REXCVAR_DEFINE_INT32(masseffect_native_verify_n, 2048, "Mass Effect",
                     "Self-check of the switches above: the old and the new path are both evaluated for the first N "
                     "uses and compared; any difference logs DIFFERENCE and turns that switch off. 0 = no check")
    .range(0, 2147483647)  // banjo-nx: banjo_cpu_verify checks every use
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_native_vs_pruned_outputs, true, "Mass Effect",
                    "Vertex shaders (package v30+) write only the outputs the pipeline's pixel shader reads "
                    "(specialization constant 60). false = all 22 outputs, as before. banjo-nx: Banjo's shader "
                    "packages have it from 2026-10-15 (before, the constant was passed and ignored); in the non-CPU "
                    "group (banjo_noncpu_enhancements = false turns it off). Exact: the pixel shader never reads the "
                    "outputs it removes. A pipeline without a pixel shader keeps only the position");

// src\me\me_ring_wait.cpp
REXCVAR_DEFINE_BOOL(masseffect_notify_no_lock, false, "Mass Effect",
                    "Ring thread: NotifyRingProgress takes the progress mutex only when the game's D3D wait is "
                    "registered as sleeping (sequentially consistent handshake on both sides) instead of on every call "
                    "(one per fence write and per ring segment). Same wake-ups")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// src\me\masseffect\masseffect_native_draws.cpp
REXCVAR_DEFINE_BOOL(masseffect_nvk_cache_per_stage, false, "Mass Effect",
                    "NVK: per-stage shader cache keys (runtime disable_lto), so a VS or PS already compiled for "
                    "another pipeline is not compiled again; needs the patched driver");
