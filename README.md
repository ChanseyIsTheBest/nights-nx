<h1 align=center>NiGHTS into Dreams... — Nintendo Switch port</h1>

A native Nintendo Switch port of the Xbox 360 (Xbox Live Arcade) version of **NiGHTS into Dreams...** (title
`58411268`, with its title update).

It is not an emulator. The PowerPC code of the game's `default.xex` is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch, and the game is drawn by Renut's
Direct3D-level renderer ([reNut](https://github.com/masterspike52/reNut) by masterspike52) directly on the Switch's
own graphics API, deko3d.

Since 1.1.0 the port is built on the ultimate base
([renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base), ports 1-254): the base's whole
tree, with every universal fix and speed-up the other ports found, and NiGHTS's own layer on it
(docs/ultimate-rebase-plan.md). The base's later ports come in with `tools/ult_port_sync.py` (docs/ultimate-sync.md).

**What's new in 1.1.0**
- On the ultimate base: its renderer, its fixes and its speed-ups.
- 60 frames a second by default; 30 is the option (the game menu, or `nights_true60 = false`).
- The game menu (hold A+B+X+Y+L+R): language, frame rate, resolutions, SMAA, MSAA, controls.
- MSAA works on NiGHTS's render targets (2x / 4x from the menu).
- Machine-optimised copies of every game function the console profile found running, and a release trained with
  profile-guided optimisation on the console.
- Smaller install: the game's files packed (LZ4 / ZXC), the movies as H.265 on the Switch's video decoder and the music
  as Opus (the installer does this).
- No logging or profiling in the release (`log_level = "info"` in renut.toml turns the logs on for a bug report).

> [!NOTE]
> You need your own copy of the game: its Xbox Live Arcade package from an Xbox 360
> (`Content/0000000000000000/58411268/000D0000/`) and its title update (`000B0000/`, or the `TU_...` file of the
> console's `Cache` folder). No game files and no shaders made from them are in this repository or its releases.

## How to install

Use the **[nights-nx installer](https://chanseyisthebest.github.io/nights-nx-installer/)**
([its source](https://github.com/ChanseyIsTheBest/nights-nx-installer)): a web page that takes your two packages,
makes the game's shaders for the Switch from them in your browser (nothing is uploaded) and gives you a zip to
extract to the root of your SD card. Start `switch/nights-nx/nights-nx.nro` from the Homebrew Menu with a
title takeover (hold R while starting a game).

## Settings

`switch/nights-nx/renut.toml` is written at the first start and holds only the player's options:

| Setting | Default | |
|---|---|---|
| `language` | `"auto"` | the Switch's language; or english, japanese, french, german, spanish, italian |
| `banjo_renut_smaa` | `true` | SMAA edge smoothing on the 3D (the HUD and text stay sharp) |
| `nights_true60` | `true` | the frame rate: `true` = 60 frames a second of the game's own, one logic step a frame, the game's speed unchanged (the game's button-sequence codes still register); `false` = 30, with the camera paced evenly (each displayed frame shows one whole logic step) |
| `input_xbox_layout` | `true` | face buttons by position, as on an Xbox pad; `false` = by letter |
| `input_joycon_sideways` | `true` | single Joy-Cons held sideways, one per player |
| `input_joycon_sideways_stick` | `true` | the single Joy-Con's stick turned for the sideways grip |
| `[input_p1]` | | button mapping: the Switch buttons that press each Xbox button |

Everything else is built into the NRO: the picture at 1280x720 handheld and 1920x1080 docked, double buffering with
its input-lag fix, the game on cores 0-2 (the fourth is the system's). Any setting written in `renut.toml` overrides
the built-in one.

The game menu (hold A+B+X+Y+L+R; + closes it) changes these while you play and writes them to `renut.toml`: the
language, the frame rate, the handheld and docked resolutions, SMAA, MSAA, the controls, and the menu's own
look. Settings marked * there take effect at the next start.

## Building

The Switch build cross-compiles with devkitA64 (`tools/build_switch.bat`); the PC build (`tools/build_pc.bat`) uses
reNut's renderer on Vulkan. Both need the code generated from your own `default.xex` (ReXGlue's codegen with
`app/config/`), which is not in this repository. Notes: `docs/` (the base's building.md; NiGHTS's
nights-notes.md and nights-ultimate-notes.md).

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) (the recompiler and runtime).
- reNut's renderer by masterspike52 (its author permits its reuse in these ports).
- The ultimate base (renut_nx_ultimate_base) and the ports whose fixes it carries: its README, THIRD_PARTY_NOTICES.md
  and docs/ultimate-fixes.md name each one, port by port. banjo-nx (renut-nx) was this port's first base.
- sa2-nx (double buffering and its queue drain, the built-in settings, the language setting, the controls),
  futari-nx and ketsui-nx (the queue drain), NetherSX2_nx, battd_nx, the bejeweled3-nx / zuma ports and peggle2-nx.
- rr6-nx (the `.lz4b` reader), culdcept-nx (the movies through FFmpeg and nxvideo, the VFS index of packed files),
  sa2-nx-ult (the ADX music from Opus) and sasrt-nx (the plan), for the installer's packed files, H.265 movies and Opus
  music.
- [LZ4](https://github.com/lz4/lz4) 1.10.0 by Yann Collet (BSD-2-Clause) and [ZXC](https://github.com/hellobertrand/zxc)
  0.15.0 by Bertrand Lebonnois (BSD-3-Clause, with [rapidhash](https://github.com/Nicoshev/rapidhash), MIT): the
  decoders of the packed game files. [libopus](https://opus-codec.org) (devkitPro's `switch-libopus`, BSD-3-Clause):
  the music's decoder. [FFmpeg](https://ffmpeg.org): its 4.4 VC-1 decoder in the SDK (LGPL-2.1-or-later), and nxvideo's
  FFmpeg 7.1 with averne's nvtegra backend (devkitPro's `switch-ffmpeg`, LGPL-2.1-or-later) for the H.265 movies on
  NVDEC.

GPL-3.0 where the sources are (LICENSE). NiGHTS into Dreams... is SEGA's; this project is not affiliated with SEGA.
