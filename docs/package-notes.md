# nights-nx package: what is in it and what to test

NiGHTS into Dreams... (Xbox 360 XBLA, title 58411268, with its title update) on the Switch through ReXGlue, with
reNut's renderer on deko3d (banjo-nx's Renut; credits below). Made by `tools/make_nights_package.py`.

<!-- facts -->

## Install

1. `COPY_TO_SD_switch_nights-nx/` to `sdmc:/switch/nights-nx/`.
2. The game's extracted package (the files of the LIVE package: `default.xex`, `afs/`, `cse/`, `fs/`, `shader/`, ...)
   plus the title update's `default.xexp` beside `default.xex`, in `sdmc:/switch/nights-nx/game_root/`.
3. Launch `nights-nx.nro` with title takeover (hold R on a game: full memory). Applet mode does not have the memory.
4. No four-core mode (your call, 2026-10-29): the game keeps to cores 0-2 and leaves the fourth core to the system
   (`banjo_system_core = 0`, no forwarder NPDM written: `banjo_forwarder_program_id = ""`).

The first boot asks for game data: `headless = true` answers it with "Create Game Data" by itself (the saves go to
`sdmc:/switch/nights-nx/userdata`). `license_mask = 1` is the full game (your package's licence), not the trial.

## The settings files

- `renut.toml`: the only settings you see (your choice, 2026-10-29): `language` (auto = the Switch's language),
  `banjo_renut_smaa`, `input_xbox_layout`, `input_joycon_sideways`, `input_joycon_sideways_stick` and the
  `[input_p1]` button map. Everything else (the release picture, double buffering, every enhancement) is built into
  the NRO; a setting written in `renut.toml`, or a file of `configs/` copied over it, overrides the built-in one. The
  NRO writes this file itself when it is missing, so an update never needs to overwrite yours.
- `configs/` (each is `renut.toml` with one change; copy over `renut.toml` to try it), and two whole files:
  `renut_diagnostics.toml` (the 10 s diagnostics in the log: use it for a test whose log you send back) and
  `renut_ab.toml` (the in-run A/B):
  - `renut_exact.toml`: the game's own picture, pixel-identical (1280x720 in both modes, no SMAA).
  - `renut_no_smaa.toml`: the release resolutions without SMAA.
  - `renut_triple_buffer.toml`: three swapchain images instead of two (the input-lag A/B).
  - `renut_banjo_resolution.toml`: banjo-nx's resolutions, the 3D at 648 lines handheld and 864 docked with dynamic
    resolution (the HUD at the display's); a fallback if 1080p docked costs too much. Untested on NiGHTS.

How the release settings work on NiGHTS:
- **SMAA**: banjo-nx runs SMAA after Banjo's final composite shader; NiGHTS has none (the 3D scene goes straight into
  the back buffer, then the HUD's quads). Here it runs just before the first HUD draw that follows the scene's draws,
  once a frame (`nights_renut_smaa_before_ps = "82E6A43836DB5864"`, the HUD's pixel shader). The HUD and text are drawn
  after it, so they stay sharp. A frame of the HUD alone (a 2D menu) gets none.
- **1080p docked**: `banjo_renut_display_height_docked = 1080` (the swapchain at 1920x1080) with
  `banjo_renut_render_height_docked = 1080` (the 3D at the same size: nothing scaled). Handheld is 720 for both.
- **Double buffering** (`nights_renut_double_buffer`, from futari-nx and sa2-nx): two swapchain images instead of
  three, so a finished frame is shown at the next refresh instead of waiting behind another.
- **Its input-lag fix** (`nights_renut_queue_drain`, sa2-nx's, from ketsui-nx through futari-nx): with two images, one
  late frame can leave every later one shown a refresh late. The display thread counts the images not finished at
  the refresh they were meant for. After 60 in a row it releases the game's next frame one refresh later, once, and
  the queue empties. Not again for 5 s; if the queue keeps filling again soon after, the gap doubles, up to 5 minutes.
  NiGHTS presents every other refresh, so an image counts as late only when it misses its second refresh (sa2-nx's
  60 fps version looks at the first). The log's `[display]` line ends with "double buffering: N new images not
  finished at their refresh, M queue drains in all".
- **60 fps presentation**: shelved (your call, 2026-10-29: the in-between frames jittered in both modes);
  `nights_present_60` stays false and its configs are gone.

## What to test on the console

1. **Boot to gameplay** with `renut.toml`: the epilepsy warning, the SEGA logo movie (the small hollow squares along
   its light streaks are part of the movie: the PC shows them too), Sonic Team, the attract loop, the title, the
   menus, Spring Valley. Watch for anything that differs from the Xbox 360 or the PC build.
2. **Frame rate and timing**: the game runs at 30 (it presents at interval 2). The log's 10 s line gives game frames,
   the render thread's and the GPU's time a frame.
3. **Send back**: `sdmc:/switch/nights-nx/logs/` (the newest `renut_*.log`) and `sdmc:/switch/rex_profile.log` (the
   CPU profile; with `elf/renut_<md5>.elf` it maps to functions and, with `source/`, to lines).
4. **The A/B** (`configs/renut_ab.toml`), 10 minutes or more of gameplay.
5. **The release picture**: SMAA on the 3D (edges smoother, the HUD and text still sharp) handheld and docked, and
   the docked picture at 1920x1080. Compare with `configs/renut_exact.toml`. Docked, watch the frame rate in heavy
   scenes: 1080p with SMAA is the GPU's heaviest setting.
6. **Input lag**: `renut.toml` against `configs/renut_triple_buffer.toml`, by feel, and the `[display]` /
   `[latency]` lines (present -> display, input -> display, the drains).

## What was measured (emulator and PC are proxies; the console decides)

See docs/nights-notes.md in `source/` (the emulator runs, the guard and verify counts, the profiles) and
docs/progress-log.md (the night's timeline).

## Credits

reNut's renderer (its author permits its reuse in these ports), sa2-nx (double buffering and its queue drain),
futari-nx and ketsui-nx (the queue drain), NetherSX2_nx (four-core mode, MIT), battd_nx, the bejeweled3-nx / zuma
ports, peggle2-nx, banjo-nx (the base). GPL-3.0 where the sources are.
