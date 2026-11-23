# Render thread: what a port can turn off

Keys of the ultimate base that cost Renut's render thread time, whether or not the game needs them. Each one either
checks every draw or replay for a case that some games never have, or measures. This page says what each costs, which
line of the port's own log says whether the game needs it, and what the ports have found so far.

From blur-nx (2026-11-20; its docs/renut-host-ideas-25-1118.md, #14, #15 and #31), with banjo-nx's numbers of
2026-11-20 and the base's own key descriptions.

## The rule

- **Decide from the port's own console log**, with `log_level = "info"`, over whole gameplay sessions: every 10 s
  window of a race or a level, not the menus or the loading screens.
- **A 0 over the whole session turns the key off.** No A/B race is needed for that. A count above 0 anywhere keeps the
  base's default: there the key is doing work, and some of these save milliseconds a frame where they fire.
- **Set the key in the port's own toml, never in the base.** Another game may need what this one does not. Name the log
  line and the count in the comment, for example
  `sasrt_gpu_skip_inert_draws = false  # 0 draws skipped in every 10 s window of a race`.
- **Check again after a rebase** that changes the renderer, and after the port reaches new parts of the game.

## 1. Per-game checks on the render thread

| Key (base default) | What the render thread pays | The log line | Off when | Where it pays off |
|---|---|---|---|---|
| `ultimate_renut_replay_inherit_same` (true) | Only with `banjo_renut_replay_inherits` on (off in the base). At every replay, BuildInheritRuns' copy and a compare of about 1 KB: if the running device holds every inherited word as the recording had it, the replay keeps the record's answers | `[renut] replays with the inherited words as recorded (ultimate_renut_replay_inherit_same): N of M`, every 65,536 replays | N is 0 | A game whose replays often run on the device they were recorded on |
| `sasrt_gpu_skip_inert_draws` (true) | At every draw, DrawStateLocked's prologue tests whether the draw can write anything (colour mask, depth write, stencil, memory export) | `[sasrt gpu] GPU round ..., in the last 10 s: ... sasrt_gpu_skip_inert_draws N` (`(off) N` with the key off) | N is 0 in every gameplay window | sasrt-nx: two inert draws a frame into a 4x depth target cost the console **1.85 ms a frame** |
| `banjo_renut_stream_redeem` (true, port 145) | At every conversion of a dynamic vertex stream, a store-card check (PagesClean) of whether its memory was written since the last one | `[renut] banjo_renut_stream_redeem: N dynamic streams quiet for 60 conversions made cacheable again`, printed only when a stream is made cacheable (the first 16, then every 1,024th). No line over a whole session is 0 | No line over whole gameplay sessions | tw2de_nx: **7-7.7 ms of render thread a frame**, converting streams the game had stopped writing |

What the ports found:

| Key | blur-nx | banjo-nx | Others |
|---|---|---|---|
| `ultimate_renut_replay_inherit_same` | false: 0 of 131,072 replays (inherits on); about 0.05-0.1 ms a frame, estimated | nothing to change: inherits is off, so the check never runs | - |
| `sasrt_gpu_skip_inert_draws` | false: 0 draws skipped in every 10 s window of a race; about 0.05-0.15 ms a frame, estimated | already off: "(off) 0" in every window of its runs (55-64 windows each) | sasrt-nx: on, 1.85 ms a frame |
| `banjo_renut_stream_redeem` | not read yet (on, the base's default) | off: 0-1 streams made cacheable in 12 min of the game, at about 0.24 us of render thread a draw | tw2de_nx: on, 7-7.7 ms a frame. deadpool-nx: off, "for UE3 vertex safety" (no numbers) |

## 2. The same kind of check on the job threads

| Key (base default) | What it pays | The log line | What to set | Ports |
|---|---|---|---|---|
| `banjo_renut_tags_check` (32) | NewOp compares the whole recording device (12.9 KB) every Nth op, to catch a chunk written without a mark | `[renut] recording from marks (banjo_renut_tags): ..., N whole checks, M chunks changed without a mark` | M is 0 over whole gameplay sessions: 256, 8 times fewer checks. Any M above 0: keep 32 | blur-nx: 256 (0 unmarked chunks in 9,408 checks per 10 s at 32; about 0.05-0.08 ms a frame, estimated). banjo-nx: already 256 on the Switch in its own code (`BANJO_SWITCH_DEFAULT(256, 32)`), 0 in 202,140 checks |

256 is not strictly exact: a write without a mark (none seen in either port) is caught 256 ops later instead of 32.
The other two keys and stream redeem are exact both ways.

## 3. In a player's build: the measurements off

The A/B, profile and test tomls turn these on to measure. The player's toml turns them off; that needs no A/B.

**On by default, so set it in the player's toml:**

| Key (base default) | What it costs | Player's build |
|---|---|---|
| `switch_perf_count_calls` (true) | A clock read and two atomics at every wrapped libnx call: kicks, GPU mappings, cache cleans, yields and sleeps. The render thread makes most of the kicks and cleans. It feeds only the 10 s report's libnx line | false (blur-nx ships false) |

**Off by default. Keep them out of the player's toml, or false:**

On the Switch, `blur_renut_diag_stage_census` and `blur_renut_diag_batch_census` are compiled out unless the build
has `BANJO_SWITCH_DIAG_CENSUS` ON (port 236): their checks cost the render thread ~0.1 us a draw with the keys off.

| Key | What it costs when on |
|---|---|
| `blur_renut_diag_stage_census` | Two clock reads and two atomics a stage (blur-nx: about 0.5-1 us of a 9.5 us draw) |
| `banjo_renut_diag_draw_kinds` | Two timer reads a draw |
| `banjo_renut_diag_gpu_shaders` | A GPU timestamp at each pixel shader change: GPU and render thread time |
| `banjo_renut_diag_packet_census` | Hashes 3 KB a draw |
| `banjo_renut_diag_repeat_census` | A few ms in two frames of every 32 |
| `blur_renut_diag_batch_census` | A load and a store a count |
| `blur_renut_host_batch_verify` | The host batch's #3, #13 and #32 done both ways and compared |
| `banjo_renut_diag_fast_census`, `banjo_renut_diag_shadow_census`, `banjo_renut_diag_unleashed_census`, `banjo_renut_diag_terrain`, `banjo_renut_diag_replay_state`, `banjo_renut_diag_resolve_trace` | Counting or logging along the draw path |
| `renut_gpu_profile` (0 = off) | GPU timing by kind of work, every N seconds |
| `switch_profile_stacks` | The SDK sampler: each sample pauses one game thread (all threads, the render thread too) |

## 4. Already automatic: nothing to set

- `blur_renut_record_skip_unused` (on): while replays inherit, NewOp builds no resource objects and no slot
  extents, which those replays never use. In blur-nx building them took 0.44 + 0.39 ms a frame on the job threads.
- `banjo_renut_replay_inherits` is off in the base. With it off, `ultimate_renut_replay_inherit_same` costs nothing.
- The 10 s report's per-draw counts (the GPU round's item counts, plume's binds and barriers, the shared and
  pushed constants, the depth counters) are off with `log_level` "off", a release's (port 244). With the logs kept,
  `banjo_renut_draw_counts` = false turns them off for a measured run: those lines show zeros, the in-run A/B keeps
  its per-draw figures (banjo-nx: 0.24 us a draw of N&B's render thread).

## For the porting prompt

[porting-a-360-game-prompt.md](porting-a-360-game-prompt.md)'s step for `ultimate_renut_replay_inherit_same`,
`sasrt_gpu_skip_inert_draws` and `banjo_renut_tags_check` (port 177) has these two as well (2026-11-20):

- `banjo_renut_stream_redeem` (true): no "made cacheable again" line over whole gameplay sessions: false.
- `switch_perf_count_calls` (true): false in the player's toml.
