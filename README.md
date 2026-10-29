<h1 align=center>NiGHTS into Dreams... — Nintendo Switch port</h1>

A native Nintendo Switch port of the Xbox 360 (Xbox Live Arcade) version of **NiGHTS into Dreams...** (title
`58411268`, with its title update).

It is not an emulator. The PowerPC code of the game's `default.xex` is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch, and the game is drawn by Renut's
Direct3D-level renderer ([reNut](https://github.com/masterspike52/reNut) by masterspike52, as banjo-nx carries it)
directly on the Switch's own graphics API, deko3d.

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
| `nights_true60` | `false` | the frame rate: `false` = 30, with the camera paced evenly (each displayed frame shows one whole logic step); `true` = 60 frames a second of the game's own, one logic step a frame, the game's speed unchanged |
| `input_xbox_layout` | `true` | face buttons by position, as on an Xbox pad; `false` = by letter |
| `input_joycon_sideways` | `true` | single Joy-Cons held sideways, one per player |
| `input_joycon_sideways_stick` | `true` | the single Joy-Con's stick turned for the sideways grip |
| `[input_p1]` | | button mapping: the Switch buttons that press each Xbox button |

Everything else is built into the NRO: the picture at 1280x720 handheld and 1920x1080 docked, double buffering with
its input-lag fix, the game on cores 0-2 (the fourth is the system's). Any setting written in `renut.toml` overrides
the built-in one.

## Building

The Switch build cross-compiles with devkitA64 (`tools/build_switch.bat`); the PC build (`tools/build_pc.bat`) uses
reNut's renderer on Vulkan. Both need the code generated from your own `default.xex` (ReXGlue's codegen with
`app/config/`), which is not in this repository. Notes: `docs/`.

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) (the recompiler and runtime).
- reNut's renderer by masterspike52 (its author permits its reuse in these ports), through banjo-nx (this port's
  base).
- sa2-nx (double buffering and its queue drain, the built-in settings, the language setting, the controls),
  futari-nx and ketsui-nx (the queue drain), NetherSX2_nx, battd_nx, the bejeweled3-nx / zuma ports and peggle2-nx.

GPL-3.0 where the sources are (LICENSE). NiGHTS into Dreams... is SEGA's; this project is not affiliated with SEGA.
