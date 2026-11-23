# The ultimate base in nights-nx: where the branch is

| | |
|---|---|
| Base | renut_nx_ultimate_base, `D:\renut-ultimate\base`, branch `ultimate` |
| Taken up to | **2705e5bc** (2026-11-22, "docs: how to check the inexact forms on a game"; ports 1-254) |
| How | the rebase of 2026-11-22 (docs/ultimate-rebase-plan.md), not ult_port_sync |

## Taking the base's later ports

As the base's docs/building.md section 7 says, a port commit at a time:

```
python tools/ult_port_sync.py --base D:\renut-ultimate\base --port D:\nights\nights-nx-ult --range 2705e5bc..<base commit> --dry-run
```

then without `--dry-run`, resolve the diff3 blocks, build, test, and write the new row here. The tool's default
`--hand` list (the per-XDK files) is right for nights-nx: those are kept at NiGHTS's XDK (2.0.21173, Peggle 2's
21256 layout) and a port's change to them comes in by hand.

What the tool cannot know about this tree:

- **Per-XDK values in shared files**, which a port's diff may carry at Katamari's (XDK 2.0.5632) numbers. NiGHTS's:
  d3d.h `kDeviceOffsetViewport` 0x32E0; render/draw.h `kDeviceOwnDeclarationBegin` and draw.cpp
  `kDeviceDeclarationOffset` 0x3118; d3d/hooks/device.cpp's scaler copy +0x3730 and gamma ramp +0x3C30; draw.h's
  field map; backend/queued.cpp's private unread words 11008-12224 and 40 read chunks; draw.cpp's census tail (11008,
  12224); draw.cpp's Present swap block +0x36DC. A new hex device offset in a port is checked against NiGHTS's
  d3d_native.h `native::off` (docs/nights-notes.md, "Renut's Direct3D layer at NiGHTS's addresses").
- **The tiling hooks** are 1.0.2's form (d3d/hooks/draw.cpp), not the base's Katamari form: a port's change to
  BeginTilingImpl / EndTilingImpl comes in by hand.
- **The frame pacing** is NiGHTS's (src/nights_frame_hooks.cpp, at the game's presentation interval); the base's frame
  limiter (src/nights_keys.cpp renutFrameLimit) runs at the end of Renut's Swap with frame_cap Off.
- **Game addresses in the base's tools**: inline_memcalls.py's defaults, leaf_dead_stores.py's entry point,
  lr_preserve.py / r1_preserve.py's no-return lists, post_codegen.py's switch_goto list (app/config/switch_goto.txt)
  hold NiGHTS's values; a port's change to those lines keeps them.
- **The docs**: the base's pages stay in docs/ (the tool skips docs/ anyway); NiGHTS's records are docs/nights-*.md.

## Record

| Date | Base commit | Ports | Notes |
|---|---|---|---|
| 2026-11-22 | 2705e5bc | 1-254 | the rebase (docs/ultimate-rebase-plan.md) |
