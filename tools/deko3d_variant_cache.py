#!/usr/bin/env python3
"""banjo-nx (deko3d renderer, docs/deko3d-renderer.md section 5): the DKSH cache, compiled on the PC.

    python tools/deko3d_variant_cache.py [--list pipeline_list.bin] [--variants deko3d_variants.txt]
        [--glsl out/deko3d_shaders/glsl] [--out deko3d_shaders.bin] [--merge existing.bin] [--jobs 12]

The variants come from Renut's pipeline lists (renut_pipeline_list, render/pipeline.cpp: every pipeline a draw needed,
the key with its shaders' hashes) or from banjo_deko3d_variant_log ("vs <HASH> ps <HASH> id=value ...", one line a
graphics pipeline). A list key's constants are what Build() gives it: 1000 + N for each bool of bool_mask (its value
from bool_values), 900 = blend_skip, 901 = output_trim and 903 = alpha_func when not 0; a specialised key adds its
generic one (no bools, no alpha function),
which draws while the specialised pipeline is built. For each stage the text is what plume_deko3d's loadVariant compiles: the shader's GLSL with
"#define SPIRV_CROSS_CONSTANT_ID_<id> <value>u" after its first line for each constant the GLSL has whose value differs
from its default (ids ascending); the key is FNV-1a 64 of that text. Each unique text is compiled by uam (the PC build
of thirdparty/uam, the same patches as the console's) and written in the cache's format (banjo_renut_deko3d.cpp):
"BDKC", version 1, the 16-byte compiler tag, then records (u64 key, u32 size, DKSH). The console reads it next to the
NRO and adds what it compiles itself. Game data: the build tree and the SD card only.
"""
import argparse
import concurrent.futures
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UAM = os.path.join(ROOT, 'out', 'deko3d_tools', 'uam-build', 'uam.exe')
TAG = b'uam110-sw8-bnx4\0'  # banjo_renut_deko3d.cpp kCacheTag (16 bytes with its terminator)
DEFINE_RE = re.compile(r'#define SPIRV_CROSS_CONSTANT_ID_(\d+) (\S+)')


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def c_strtoul(text):
    """strtoul(text, nullptr, 0) as plume_deko3d's parseSpecDefaults reads a default ("0u", "2u")."""
    m = re.match(r'\s*(0[xX][0-9a-fA-F]+|0[0-7]*|[1-9][0-9]*)', text)
    if not m:
        return 0
    return int(m.group(1), 0) & 0xFFFFFFFF


def spec_defaults(glsl):
    return sorted({int(m.group(1)): c_strtoul(m.group(2)) for m in DEFINE_RE.finditer(glsl)}.items())


def variant_defines(defaults, spec):
    defines = ''
    for cid, default in defaults:
        if cid in spec and spec[cid] != default:
            defines += f'#define SPIRV_CROSS_CONSTANT_ID_{cid} {spec[cid]}u\n'
    return defines


def variant_text(glsl, defines):
    if not defines:
        return glsl
    nl = glsl.find('\n')
    if nl < 0:
        return glsl
    return glsl[:nl + 1] + defines + glsl[nl + 1:]


def compile_one(item):
    key, stage, text = item
    if callable(text):
        text = text()
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'v.glsl')
        out = os.path.join(tmp, 'v.dksh')
        with open(src, 'wb') as f:
            f.write(text)
        r = subprocess.run([UAM, '-s', stage, '-o', out, src], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(out):
            return key, None, (r.stdout + r.stderr).strip()[:300]
        return key, open(out, 'rb').read(), ''


def read_cache(path):
    entries = {}
    if not path or not os.path.isfile(path):
        return entries
    data = open(path, 'rb').read()
    if data[:4] != b'BDKC' or data[8:24] != TAG:
        print(f'deko3d_variant_cache: {path} is another compiler\'s cache: not merged')
        return entries
    # As LoadCache (banjo_renut_deko3d.cpp): records up to the first that is not whole (its size past the end, or its
    # data not a DKSH: an append the console did not finish).
    at = 24
    while at + 16 <= len(data):
        key, size = struct.unpack_from('<QI', data, at)
        if size < 64 or at + 12 + size > len(data) or data[at + 12:at + 16] != b'DKSH':
            print(f'deko3d_variant_cache: {path}: {len(data) - at} bytes after the last whole record left out')
            break
        entries[key] = data[at + 12:at + 12 + size]
        at += 12 + size
    return entries


# render/pipeline.cpp: ListHeader (magic "RNPL", version 3 or 4, key size, streams), then records (u32 size, the key
# ...). PipelineKey: two shader hashes (u64), layout hash, topology, 4 colour formats, depth format, colour count,
# samples, 4 blend controls, 9 more u32 fields (offset 108 on), bool_mask[8], bool_values[8], blend_skip, output_trim;
# version 4 (2026-10-18) adds alpha_func at 180 (constant 903 + 1, banjo_deko3d_alpha_spec; version 3 had padding there)
# and a reserved word.
LIST_KEY_SIZES = {3: 184, 4: 192}
LIST_KEY_SIZE = 184
OFF_BOOL_MASK, OFF_BOOL_VALUES, OFF_BLEND_SKIP, OFF_OUTPUT_TRIM, OFF_ALPHA_FUNC = 108, 140, 172, 176, 180
OFF_PER_INSTANCE, OFF_RECT_LIST, OFF_POINT_SPRITE = 96, 100, 104
OFF_DEPTH_CONTROL = 76  # banjo-nx (2026-10-19): RB_DEPTHCONTROL (constant 909, banjo_deko3d_early_z)
OFF_STENCIL_REF_MASK = 80  # 2026-10-20: ref, read mask, write mask (bits 16-23)
# Pseudo constants in a pipeline's spec (never a shader's id): a tessellated draw's, one with a geometry shader, and
# (2026-10-19) whether the draw writes depth or stencil (depth_writes below).
FLAG_TESS, FLAG_GS, FLAG_DEPTH_WRITES = -1, -2, -3


def depth_writes(dc, stencil_ref_mask=None):
    """render/pipeline.cpp's rule for constant 909 (2026-10-20): the draw writes depth (bit 2), or its stencil test
    can write (bit 0, a write mask, an op of either face not KEEP: bits 11-19, and 23-31 with bit 7). Without the
    mask (a variants-log line) a test with any op not KEEP counts as writing."""
    if dc & 4:
        return 1
    if not dc & 1:
        return 0
    ops = ((dc >> 11) & 0x1FF) != 0 or (dc & 0x80 and ((dc >> 23) & 0x1FF) != 0)
    if stencil_ref_mask is not None and not (stencil_ref_mask >> 16) & 0xFF:
        return 0
    return 1 if ops else 0

LOC_IN_RE = re.compile(r'^layout\(location = (\d+)\) in ', re.M)
LOC_OUT_RE = re.compile(r'^layout\(location = (\d+)\) out ', re.M)


_MASKS = {}


def location_mask(text, regex):
    # Memoised by the text's identity (the callers keep each shader's text): a list has tens of thousands of pipelines
    # over a few thousand shaders, and the regex over a vertex shader's whole text per pipeline took most of the time.
    key = (id(text), regex.pattern)
    hit = _MASKS.get(key)
    if hit is not None and hit[0] is text:
        return hit[1]
    mask = 0
    for m in regex.finditer(text):
        if int(m.group(1)) < 32:
            mask |= 1 << int(m.group(1))
    _MASKS[key] = (text, mask)
    return mask


_VSZERO = {}


def vs_zero_mask(text):
    hit = _VSZERO.get(id(text))
    if hit is not None and hit[0] is text:
        return hit[1]
    m = re.search(r'^// banjo-vszero=0x([0-9a-f]+)$', text, re.M)
    v = int(m.group(1), 16) if m else 0
    _VSZERO[id(text)] = (text, v)
    return v


def stage_spec(prefix, spec, vs_text, ps_text, ps_present=True):
    """The constants one stage of a list pipeline gets, as render/pipeline.cpp and plume_deko3d give them (2026-10-18,
    docs/deko3d-shader-optimisations.md): 904 = 1 unless the pipeline is a tessellated draw's (banjo_deko3d_tess_spec);
    the vertex shader's 905, the locations its pixel shader reads of the ones it writes, when that is not all of them
    (none without a pixel shader; left out with a geometry shader, which reads them all)."""
    out = {k: v for k, v in spec.items() if k >= 0}
    if not spec.get(FLAG_TESS, 0):
        out[904] = 1
    if (prefix == 'vs' and not spec.get(FLAG_GS, 0) and vs_text is not None and
            'SPIRV_CROSS_CONSTANT_ID_905' in vs_text and (ps_text is not None or not ps_present)):
        outs = location_mask(vs_text, LOC_OUT_RE)
        mask = (location_mask(ps_text, LOC_IN_RE) if ps_text is not None else 0) & outs
        if mask != outs:
            out[905] = mask
    # the pixel shader's 909 (2026-10-19, banjo_deko3d_early_z): depth tested first where the draw writes neither
    # depth nor stencil (a variants-log line has no flag: the renderer logs 909 itself)
    if (prefix == 'ps' and ps_text is not None and 'SPIRV_CROSS_CONSTANT_ID_909' in ps_text and
            FLAG_DEPTH_WRITES in spec and not spec[FLAG_DEPTH_WRITES]):
        out[909] = 1
    # the pixel shader's 906: its inputs the vertex shader writes as zero (deko3d_glsl_opt.py vszero / pszero)
    if (prefix == 'ps' and not spec.get(FLAG_GS, 0) and vs_text is not None and ps_text is not None and
            'SPIRV_CROSS_CONSTANT_ID_906' in ps_text):
        zero = vs_zero_mask(vs_text) & location_mask(ps_text, LOC_IN_RE)
        if zero:
            out[906] = zero
    return out


# 2026-10-18: alpha functions (constant 903 = function + 1) assumed for a list pipeline that records none (a version 3
# list, or a record converted from one): 8 (no alpha test) and 7 (>=, the alpha-to-coverage test at 0.5), the two the
# Ryujinx runs saw 161 of 164 times, and 5 (>, 103 of the 183 variants run 47 compiled during play); the console builds
# the specialised pipelines from them without compiling.
ALPHA_GUESS = (8, 7, 5)


def read_list(path, blend_skip_on, alpha_guess=ALPHA_GUESS):
    """blend_skip_on: keep the keys' blend skip (constant 900); off (Renut's renderer since package 30,
    banjo_renut_blend_skip) the pipelines are built without it, whatever the list recorded. alpha_guess: the
    alpha functions (903 values) also made for a pipeline with a pixel shader whose record has none."""
    data = open(path, 'rb').read()
    magic, version, key_size, streams = struct.unpack_from('<4sIII', data, 0)
    if magic != b'RNPL' or LIST_KEY_SIZES.get(version) != key_size:
        sys.exit(f'deko3d_variant_cache: {path}: not a version 3 or 4 pipeline list ({magic}, {version}, {key_size})')
    at = 16
    out = []
    while at + 4 <= len(data):
        (size,) = struct.unpack_from('<I', data, at)
        at += 4
        if at + size > len(data) or size < key_size:
            break
        vs, ps = struct.unpack_from('<QQ', data, at)
        mask = struct.unpack_from('<8I', data, at + OFF_BOOL_MASK)
        values = struct.unpack_from('<8I', data, at + OFF_BOOL_VALUES)
        blend_skip, output_trim = struct.unpack_from('<II', data, at + OFF_BLEND_SKIP)
        alpha_func = struct.unpack_from('<I', data, at + OFF_ALPHA_FUNC)[0] if version >= 4 else 0
        per_instance, rect_list, point_sprite = struct.unpack_from('<III', data, at + OFF_PER_INSTANCE)
        depth_control, stencil_ref_mask = struct.unpack_from('<II', data, at + OFF_DEPTH_CONTROL)
        at += size
        tail = {FLAG_TESS: per_instance, FLAG_GS: rect_list | point_sprite,
                FLAG_DEPTH_WRITES: depth_writes(depth_control, stencil_ref_mask)}
        if blend_skip and blend_skip_on:
            tail[900] = blend_skip
        if output_trim:
            tail[901] = output_trim
        spec = dict(tail)
        if alpha_func:
            spec[903] = alpha_func
        for w in range(8):
            for b in range(32):
                if mask[w] >> b & 1:
                    spec[1000 + w * 32 + b] = values[w] >> b & 1
        hashes = (f'{vs:016X}', f'{ps:016X}')
        out.append((hashes, spec))
        if any(mask) or alpha_func:
            out.append((hashes, tail))  # the generic pipeline a specialised key draws with until it is built
        if not alpha_func and ps:
            for guess in alpha_guess:
                guessed = dict(spec)
                guessed[903] = guess
                out.append((hashes, guessed))
    return out


def config_specs(prefix, defaults, sspec):
    """A stage's constants and the configuration's A/B variants of them (the keys' state does not decide these)."""
    # banjo_deko3d_early_hoist off (constant 907 = 0) too, for the pixel shaders that have it
    sspecs = [sspec]
    if prefix == 'ps' and any(cid == 907 for cid, _ in defaults):
        sspecs.append({**sspec, 907: 0})
    # banjo_deko3d_txq_constants off (constant 908 = 0, 2026-10-19) too, for the pixel shaders that have it
    if prefix == 'ps' and any(cid == 908 for cid, _ in defaults):
        sspecs += [{**s, 908: 0} for s in list(sspecs)]
    # banjo_deko3d_gpr32 on (constant 910 = 1, 2026-10-19, an A/B) too, for the pixel shaders that have it
    if prefix == 'ps' and any(cid == 910 for cid, _ in defaults):
        sspecs += [{**s, 910: 1} for s in list(sspecs) if 908 not in s]
    # banjo_deko3d_light_branch on (constant 911 = 1, 2026-10-20, an A/B) too, for the pixel shaders that have it;
    # 2026-10-20 evening: with gpr32 on as well (910 = 1 and 911 = 1: the fur family is on both lists), for the
    # A/B pack that measures both in one run (banjo_renut_ab_targets "15,16").
    if prefix == 'ps' and any(cid == 911 for cid, _ in defaults):
        sspecs += [{**s, 911: 1} for s in list(sspecs) if 908 not in s]
    # banjo_deko3d_skin_once on (constant 913 = 1, 2026-10-20, an A/B) too, for the vertex shaders that have it
    if prefix == 'vs' and any(cid == 913 for cid, _ in defaults):
        sspecs += [{**s, 913: 1} for s in list(sspecs)]
    return sspecs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--generic', action='store_true',
                    help="also every GLSL file's generic variant (2026-10-21, banjo_deko3d_async_variants: what a draw "
                         "uses while its own variant compiles in the background; no constant of the key's state, the "
                         "configuration's A/B ones as for the pipelines)")
    ap.add_argument('--variants', action='append', default=[])
    ap.add_argument('--list', action='append', default=[], help="a Renut pipeline list (renut_pipeline_list)")
    ap.add_argument('--glsl', default=os.path.join(ROOT, 'out', 'deko3d_shaders', 'glsl_opt'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'deko3d_shaders', 'deko3d_shaders.bin'))
    ap.add_argument('--merge', default=None, help='an existing cache whose entries are kept')
    ap.add_argument('--dry-run', action='store_true', help='count the variants (and the merged cache entries among them)')
    ap.add_argument('--prune', action='store_true',
                    help='keep only the variants of these lists (a merged cache gives its compiled code, not its other '
                         'entries): for a package')
    ap.add_argument('--alpha-guess', default=','.join(map(str, ALPHA_GUESS)),
                    help='903 values also made for a list pipeline with no alpha function recorded ("" for none)')
    ap.add_argument('--blend-skip', action='store_true',
                    help="keep the lists' blend skip (banjo_renut_blend_skip = true); by default it is left out")
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument('--export-variants', default=None,
                    help="write every variant as '<vs|ps>_<HASH> <id>=<value>,...' (the constants that differ from the "
                         "shader's defaults, ids ascending): the banjo-nx installer's list. Metadata only (shader hashes "
                         "and numbers); each line is checked to give its key again from the GLSL")
    ap.add_argument('--runtime-log', action='append', default=[],
                    help="a console's renut log: the run-time variants it names (base size and hash) are added")
    a = ap.parse_args()

    glsl_cache = {}
    signature = {}  # name -> 'size:mtime_ns' of the GLSL file (the key memo below is by it)

    def glsl_of(prefix, h):
        """The shader's GLSL and its constants' defaults (None when there is no GLSL)."""
        name = f'{prefix}_{h}.glsl'
        if name not in glsl_cache:
            p = os.path.join(a.glsl, name)
            text = open(p, 'rb').read().decode('utf-8') if os.path.isfile(p) else None
            glsl_cache[name] = (text, spec_defaults(text)) if text is not None else None
            if text is not None:
                st = os.stat(p)
                signature[name] = f'{st.st_size}:{st.st_mtime_ns}'
        return glsl_cache[name]

    # The variant keys of earlier runs (FNV-1a of each variant's text in pure Python was most of a run's time): by the
    # GLSL file's name, size and modification time and the defines. Build tree only (out/deko3d_shaders).
    memo_path = os.path.join(ROOT, 'out', 'deko3d_shaders', 'variant_keys.json')
    try:
        memo = json.load(open(memo_path, encoding='utf-8'))
    except (OSError, ValueError):
        memo = {}
    memo_new = {}

    pipelines = []
    for path in a.list:
        pipelines += read_list(path, a.blend_skip, tuple(int(x) for x in a.alpha_guess.split(',') if x))
    for path in a.variants:
        for line in open(path, encoding='utf-8', errors='replace'):
            parts = line.split()
            if len(parts) < 4 or parts[0] != 'vs' or parts[2] != 'ps':
                continue
            spec = {}
            for p in parts[4:]:
                k, _, v = p.partition('=')
                if k.isdigit() and v.isdigit():
                    spec[int(k)] = int(v) & 0xFFFFFFFF
            spec[FLAG_TESS] = 1  # the log has render/pipeline.cpp's constants (904 among them) as they were
            # 2026-10-19: the line's depth control (after '|': "depth XXXXXXXX") gives constant 909 for lines written
            # before the renderer logged it (banjo_deko3d_early_z).
            if 'depth' in parts:
                at = parts.index('depth')
                if at + 1 < len(parts):
                    try:
                        spec[FLAG_DEPTH_WRITES] = depth_writes(int(parts[at + 1], 16))
                    except ValueError:
                        pass
            # 2026-10-20: "gs N" (a point sprite or rectangle list pipeline: the geometry shader reads every output,
            # so no 905 / 906); lines without it are taken as having none.
            if 'gs' in parts:
                at = parts.index('gs')
                if at + 1 < len(parts) and parts[at + 1].isdigit():
                    spec[FLAG_GS] = int(parts[at + 1])
            pipelines.append(((parts[1], parts[3]), spec))
    if not pipelines and not a.generic:
        sys.exit('deko3d_variant_cache: no pipelines (--list or --variants)')

    todo = {}

    names = {}  # 2026-10-20: what each variant is, for a short list of the ones compiled
    seen = set()  # (shader, defines) already hashed
    missing = 0
    lines = len(pipelines)
    for (vs_hash, ps_hash), spec in pipelines:
        vs_shader = glsl_of('vs', vs_hash)
        no_ps = ps_hash == '0000000000000000'
        ps_shader = None if no_ps else glsl_of('ps', ps_hash)
        for prefix, h, stage in (('vs', vs_hash, 'vert'), ('ps', ps_hash, 'frag')):
            if h == '0000000000000000':
                continue
            shader = vs_shader if prefix == 'vs' else ps_shader
            if shader is None:
                missing += 1
                continue
            sspec = stage_spec(prefix, spec, vs_shader[0] if vs_shader else None,
                               ps_shader[0] if ps_shader else None, not no_ps)
            for sspec in config_specs(prefix, shader[1], sspec):
                defines = variant_defines(shader[1], sspec)
                if (prefix, h, defines) in seen:
                    continue
                seen.add((prefix, h, defines))
                name = f'{prefix}_{h}.glsl'
                mk = f'{name}|{signature[name]}|{defines}'
                key = memo.get(mk)
                if key is None:
                    key = fnv1a64(variant_text(shader[0], defines).encode('utf-8'))
                memo_new[mk] = key
                todo.setdefault(key, (key, stage, lambda g=shader[0], d=defines: variant_text(g, d).encode('utf-8')))
                names.setdefault(key, f'{name} {defines}')
    # 2026-10-21 (banjo_deko3d_async_variants): every shader's generic variant, so no draw waits for a compile: plume
    # draws with it (the key's state constants left out: the bools, 900, 901, 903-906, 909) while the draw's own variant
    # compiles in the background.
    if a.generic:
        gen_added = 0
        for name in sorted(os.listdir(a.glsl)):
            m = re.fullmatch(r'(vs|ps)_([0-9A-F]{16})\.glsl', name)
            if not m:
                continue
            prefix, h = m.group(1), m.group(2)
            shader = glsl_of(prefix, h)
            if shader is None:
                continue
            for sspec in config_specs(prefix, shader[1], {}):
                defines = variant_defines(shader[1], sspec)
                if (prefix, h, defines) in seen:
                    continue
                seen.add((prefix, h, defines))
                mk = f'{name}|{signature[name]}|{defines}'
                key = memo.get(mk)
                if key is None:
                    key = fnv1a64(variant_text(shader[0], defines).encode('utf-8'))
                memo_new[mk] = key
                todo.setdefault(key, (key, 'vert' if prefix == 'vs' else 'frag',
                                      lambda g=shader[0], d=defines: variant_text(g, d).encode('utf-8')))
                names.setdefault(key, f'{name} {defines} (generic)')
                gen_added += 1
        print(f'deko3d_variant_cache: generic variants: {gen_added} the lists did not have')
    # 2026-10-20: the variants a console compiled in play, from its renut logs ("uam: run-time variant (stage S, N bytes,
    # base M bytes HASH): defines"): the shader is the GLSL file of M bytes whose FNV-1a 64 is HASH, the variant's
    # defines the first ascending run of the line's constants (as plume inserted them after the first line).
    rt_added = rt_unmatched = rt_known = 0
    if a.runtime_log:
        by_size = {}
        for name in os.listdir(a.glsl):
            if re.fullmatch(r'(vs|ps)_[0-9A-F]{16}\.glsl', name):
                by_size.setdefault(os.path.getsize(os.path.join(a.glsl, name)), []).append(name)
        fnv_of = {}
        rt_re = re.compile(r'run-time variant \(stage (\d+), \d+ bytes, base (\d+) bytes ([0-9a-f]{16})\): (.*)$')
        for path in a.runtime_log:
            for line in open(path, encoding='utf-8', errors='replace'):
                m = rt_re.search(line)
                if not m:
                    continue
                stage, size, want = int(m.group(1)), int(m.group(2)), int(m.group(3), 16)
                match = None
                for name in by_size.get(size, []):
                    if name not in fnv_of:
                        fnv_of[name] = fnv1a64(open(os.path.join(a.glsl, name), 'rb').read())
                    if fnv_of[name] == want:
                        match = name
                        break
                if match is None:
                    rt_unmatched += 1
                    continue
                defines, last = '', -1
                for item in [x.strip() for x in m.group(4).split(';') if x.strip()]:
                    parts = item.split()
                    if len(parts) != 2 or not parts[0].isdigit() or int(parts[0]) <= last:
                        break
                    last = int(parts[0])
                    defines += f'#define SPIRV_CROSS_CONSTANT_ID_{parts[0]} {parts[1]}\n'
                prefix = match[:2]
                if (prefix, match[3:19], defines) in seen:
                    rt_known += 1  # the lists made this variant already
                    continue
                seen.add((prefix, match[3:19], defines))
                text = open(os.path.join(a.glsl, match), 'rb').read().decode('utf-8')
                key = fnv1a64(variant_text(text, defines).encode('utf-8'))
                todo.setdefault(key, (key, 'vert' if stage == 0 else 'frag',
                                      lambda g=text, d=defines: variant_text(g, d).encode('utf-8')))
                names.setdefault(key, f'{match} {defines}')
                rt_added += 1
        print(f'deko3d_variant_cache: run-time log variants: {rt_added} added, {rt_known} the lists had already, '
              f'{rt_unmatched} without their GLSL (another shader package)')
    os.makedirs(os.path.dirname(memo_path), exist_ok=True)
    json.dump(dict(memo, **memo_new), open(memo_path + '.tmp', 'w', encoding='utf-8'))
    os.replace(memo_path + '.tmp', memo_path)

    if a.export_variants:
        # 2026-10-22 (the banjo-nx installer, which compiles the variants in the player's browser): names[key] is
        # "<file> <the #define lines>"; written as ids and values, checked against the key.
        define_re = re.compile(r'#define SPIRV_CROSS_CONSTANT_ID_(\d+) (\d+)u\n')
        lines_out = []
        for key in sorted(todo):
            name = names.get(key)
            if name is None:
                sys.exit(f'deko3d_variant_cache: --export-variants: no name for {key:016X}')
            stem, _, rest = name.partition('.glsl ')
            rest = rest.replace(' (generic)', '')
            pairs = define_re.findall(rest)
            defines = ''.join(f'#define SPIRV_CROSS_CONSTANT_ID_{i} {v}u\n' for i, v in pairs)
            if defines != rest:
                sys.exit(f'deko3d_variant_cache: --export-variants: unexpected defines for {stem}: {rest!r}')
            glsl = open(os.path.join(a.glsl, stem + '.glsl'), encoding='utf-8').read()
            if fnv1a64(variant_text(glsl, defines).encode('utf-8')) != key:
                sys.exit(f'deko3d_variant_cache: --export-variants: {stem} {pairs} does not give {key:016X}')
            lines_out.append(f'{stem} ' + ','.join(f'{i}={v}' for i, v in pairs))
        with open(a.export_variants, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines_out) + '\n')
        print(f'deko3d_variant_cache: {len(lines_out)} variants written to {a.export_variants} (every key checked)')
    entries = read_cache(a.merge)
    if a.dry_run:
        known = sum(1 for key in entries if key in todo)
        print(f'deko3d_variant_cache: {lines} pipelines, {len(todo)} stage variants, {missing} stages without GLSL; '
              f'{known} of the {len(entries)} in the merged cache are among them')
        return 0
    work = [item for key, item in todo.items() if key not in entries]
    print(f'deko3d_variant_cache: {lines} pipelines, {len(todo)} stage variants ({len(work)} to compile, '
          f'{len(todo) - len(work)} in the merged cache), {missing} stages without GLSL')
    if a.merge and len(work) <= 32:
        for key, _, _ in work:  # 2026-10-20: the few a merge compiles, by name (what the lists alone did not give)
            print(f'  to compile: {key:016X} {names.get(key, "?")}')
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        for i, (key, dksh, err) in enumerate(ex.map(compile_one, work)):
            if dksh is None:
                failed += 1
                if failed <= 10:
                    print(f'  uam failed for {key:016X}: {err}')
            else:
                entries[key] = dksh
            if (i + 1) % 200 == 0:
                print(f'  {i + 1}/{len(work)}', flush=True)
    if a.prune:
        entries = {k: v for k, v in entries.items() if k in todo}
    out = bytearray(b'BDKC' + struct.pack('<I', 1) + TAG)
    for key in sorted(entries):
        out += struct.pack('<QI', key, len(entries[key])) + entries[key]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out + '.tmp', 'wb').write(out)
    os.replace(a.out + '.tmp', a.out)
    print(f'deko3d_variant_cache: {len(entries)} variants, {len(out) >> 10} KB -> {a.out} ({failed} failed)')


if __name__ == '__main__':
    sys.exit(main())
