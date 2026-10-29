#!/usr/bin/env python3
"""nights-nx (2026-10-28): the hand edits that move banjo-nx's Renut renderer to NiGHTS (XDK 2.0.21173, Peggle 2's
21256 device layout), after tools/renut_hook_remap_nights.py (hooks) and tools/renut_device_offsets.py (peggle2-nx's
152 device-offset sites). docs/nights-notes.md, "Renut's Direct3D layer at NiGHTS's addresses", has the evidence.

  1. guest data addresses (N&B -> NiGHTS), each found by content or by the instruction of the paired function;
  2. peggle2-nx's XDK-behaviour fixes, which hold for 21173 (Zuma's Revenge's 20871 tree has them too):
     SetTexture(NULL) clears the fetch constant's type bits; BeginExport draws the NaN vertex whenever no export is
     active; no +16712 clear in InitializeTitleDevice; the 32-byte hardware block at device+11028;
  3. raw N&B device offsets in banjo-nx's code written after peggle2-nx's site list (2026-10-19), to the 21256 layout.
Each edit is an exact (old, new) text pair; a pair whose new text is already there is skipped (idempotent), one whose
old text is missing is reported.

  python -I tools/nights_renut_port.py [--check]
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GPU = os.path.join(ROOT, 'app', 'src', 'renut_engine', 'gpu')

EDITS = [
    # 1. Guest data addresses.
    ('d3d/d3d_native_state.cpp', 'kRenderStateTable = 0x82E506F0;', 'kRenderStateTable = 0x82600380;  // nights-nx: N&B 0x82E506F0, Peggle 2 0x828DFDB8'),
    ('d3d/d3d_native_state.cpp', 'kSamplerStateTable = 0x82E50BD0;', 'kSamplerStateTable = 0x82600840;  // nights-nx: N&B 0x82E50BD0, Peggle 2 0x828E0278'),
    ('d3d/d3d_native_state.cpp', 'kFormatTable = 0x820052F8;', 'kFormatTable = 0x82001338;  // nights-nx: N&B 0x820052F8 (128 bytes identical)'),
    ('d3d/d3d_native_state.cpp', 'kSkipExportNanVertex = 0x82FAD204;', 'kSkipExportNanVertex = 0x82798DA0;  // nights-nx: N&B 0x82FAD204'),
    ('d3d/d3d_native_state.cpp', 'kSkipExport512Pixels = 0x82FAD208;', 'kSkipExport512Pixels = 0x82798DA4;  // nights-nx: N&B 0x82FAD208'),
    ('d3d/hooks/device.cpp', 'kDeviceCapsVa = 0x82183B00;', 'kDeviceCapsVa = 0x82001548;  // nights-nx: N&B 0x82183B00 (304 bytes identical)'),
    ('d3d/hooks/device.cpp', 'kGlobalQwordVa = 0x82F39B38;', 'kGlobalQwordVa = 0x82600930;  // nights-nx: N&B 0x82F39B38 (the build qword, 21173)'),
    ('d3d/hooks/device.cpp', 'kVdGlobalDeviceImportSlot = 0x820007FC;', 'kVdGlobalDeviceImportSlot = 0x82000598;  // nights-nx: N&B 0x820007FC'),
    ('d3d/hooks/draw.cpp', 'kFullViewport = 0x820018DC;', 'kFullViewport = 0x8200129C;  // nights-nx: N&B 0x820018DC (24 bytes identical)'),
    ('d3d/hooks/resource.cpp', 'kFormatTable = 0x820052F8;', 'kFormatTable = 0x82001338;  // nights-nx: N&B 0x820052F8'),
    ('d3d/hooks/resource.cpp', 'kGameNoLargePagesVa = 0x82F39E34;', 'kGameNoLargePagesVa = 0x827E94E4;  // nights-nx: N&B 0x82F39E34'),
    ('d3d/hooks/resource.cpp', 'kHiZOwnerVa = 0x82FA36C8;', 'kHiZOwnerVa = 0x82799DD0;  // nights-nx: N&B 0x82FA36C8'),
    # 2. peggle2-nx's XDK-behaviour fixes.
    ('d3d/hooks/device.cpp', '  d.SetW(16712, 0);\n', '  // nights-nx (peggle2-nx): N&B cleared +16712 here; XDK 21256 / 21173 InitializeHardwareDevice has no such word.\n'),
    # 3. Raw N&B offsets in banjo-nx's newer code.
    ('backend/queued.cpp', 'constexpr u32 kDeltaPrivateUnreadBegin = 10880, kDeltaPrivateUnreadEnd = 11776;',
     'constexpr u32 kDeltaPrivateUnreadBegin = 11008, kDeltaPrivateUnreadEnd = 12224;  // nights-nx: XDK 21256 / 21173 (N&B 10880-11775)'),
    ('d3d/hooks/device.cpp', 'Video::NoteGameGamma(device + 0x3A9C);', 'Video::NoteGameGamma(device + 0x3C30);  // nights-nx: N&B 0x3A9C'),
    ('d3d/d3d_native.h', 't.chunks[n] |= TouchBits(d3d::kDeviceOffsetStreamSources, 0x3100 - d3d::kDeviceOffsetStreamSources);',
     't.chunks[n] |= TouchBits(d3d::kDeviceOffsetStreamSources, d3d::kDeviceOffsetTextures - d3d::kDeviceOffsetStreamSources);  // nights-nx: was N&B 0x3100'),
    ('render/draw.cpp', 'static_assert(kCensusTailBegin < 10880 && 11776 < d3d::kDeviceSnapshotSize);',
     'static_assert(kCensusTailBegin < 11008 && 12224 < d3d::kDeviceSnapshotSize);  // nights-nx: XDK 21256 / 21173 (N&B 10880, 11776)'),
    ('render/draw.cpp', 'state = XXH3_64bits_withSeed(device_state + kCensusTailBegin, 10880 - kCensusTailBegin, state);',
     'state = XXH3_64bits_withSeed(device_state + kCensusTailBegin, 11008 - kCensusTailBegin, state);  // nights-nx: N&B 10880'),
    ('render/draw.h', '''  if (at < 0x2A80)
    return kFieldMisc;
  if (at < 0x2E00)
    return kFieldUnread;
  if (at >= 0x2E2C && at < 0x2E30)
    return kFieldDecl;
  if (at < 0x2F00)
    return kFieldPrivate;
  if (at < 0x3094)
    return kFieldTail;
  if (at < 0x3098)
    return kFieldIndex;
  if (at < 0x30AC)
    return kFieldTargets;
  if (at < 0x3100)
    return kFieldStreams;
  if (at < 0x3168)
    return kFieldTexWords;
  if (at < 0x3184)
    return kFieldTail;
  if (at < 0x3194)
    return kFieldScissor;
  if (at < 0x319C)
    return kFieldShaders;
  if (at < 0x31A4)
    return kFieldTail;
  return kFieldPredication;''', '''  // nights-nx: XDK 21256 / 21173's layout (peggle2-nx out/renut_port/device_offsets.json). MaxAnisotropy and ZFilter
  // are public there (+0x2A70 / +0x2A8A; N&B kept private copies at +0x2E94 / +0x2EE2, which were kFieldPrivate), the
  // private words the renderer does not read are 11008-12223 (N&B 10880-11775), the declaration is +0x2FD0
  // (N&B +0x2E2C), the private render-state copies end after HighPrecisionBlend +0x3078 (N&B +0x2EFC), and the
  // snapshot's tail (index buffer ... predication) is N&B's +0x178.
  if (at < 0x2A70)
    return kFieldMisc;
  if (at < 0x2AA4)
    return kFieldPrivate;
  if (at < 0x2B00)
    return kFieldMisc;
  if (at < 0x2FC0)
    return kFieldUnread;
  if (at >= 0x2FD0 && at < 0x2FD4)
    return kFieldDecl;
  if (at < 0x3080)
    return kFieldPrivate;
  if (at < 0x320C)
    return kFieldTail;
  if (at < 0x3210)
    return kFieldIndex;
  if (at < 0x3224)
    return kFieldTargets;
  if (at < 0x3278)
    return kFieldStreams;
  if (at < 0x32E0)
    return kFieldTexWords;
  if (at < 0x32FC)
    return kFieldTail;
  if (at < 0x330C)
    return kFieldScissor;
  if (at < 0x3314)
    return kFieldShaders;
  if (at < 0x331C)
    return kFieldTail;
  return kFieldPredication;'''),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    done = skipped = 0
    missing = []
    texts = {}
    for rel, old, new in EDITS:
        path = os.path.join(GPU, rel)
        if path not in texts:
            raw = open(path, encoding='utf-8', newline='').read()
            texts[path] = [raw, raw.replace('\r\n', '\n')]
        t = texts[path][1]
        if new in t:
            skipped += 1
            continue
        if old not in t:
            missing.append(f'{rel}: not found: {old.splitlines()[0][:100]}')
            continue
        texts[path][1] = t.replace(old, new, 1)
        done += 1
    for path, (raw, t) in texts.items():
        out = t.replace('\n', '\r\n') if '\r\n' in raw else t
        if out != raw and not a.check:
            open(path, 'w', encoding='utf-8', newline='').write(out)
    print(f'nights_renut_port: {done} edits, {skipped} already done')
    for m in missing:
        print('  MISSING ' + m)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
