"""Differential test of a GLSL pass on a whole game shader (2026-10-20): the shader before and after, with the same
specialization defines, compiled by uam, decompiled by Ryujinx's translator (shtool), turned into Python and run on
random inputs and constant buffers; texture fetches are a fixed function of their binding and coordinates (the same in
both programs), the special functions Python's. Compares the outputs (and discard) bit for bit.

    python difftest_game.py <before.glsl> <after.glsl> [trials] [N=value ...]"""
import math
import os
import random
import re
import struct
import types
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ORIG = os.getcwd()
# difftest_ps compiles through dp.frag / dp.dksh / dp.bin in the working directory: one folder of its own per process,
# so runs in parallel do not read each other's files.
import tempfile  # noqa: E402
os.chdir(tempfile.mkdtemp(prefix='difftest_'))
_argv, sys.argv = sys.argv, sys.argv[:1]
import difftest_ps as d  # noqa: E402
sys.argv = _argv

f32 = d.f32


def fnan(x):
    return isinstance(x, float) and math.isnan(x)


def MIN(a, b):  # Maxwell FMNMX: a NaN operand gives the other
    if fnan(a):
        return b
    if fnan(b):
        return a
    return a if a < b else b


def MAX(a, b):
    if fnan(a):
        return b
    if fnan(b):
        return a
    return a if a > b else b


def CLAMP(x, lo, hi):
    return MIN(MAX(x, lo), hi)


def DIV(a, b):
    try:
        return f32(a / b)
    except ZeroDivisionError:
        if fnan(a) or a == 0:
            return math.nan
        return math.copysign(math.inf, a) * math.copysign(1.0, b)


def SQRT(x):
    if fnan(x) or x < 0:
        return math.nan
    return f32(math.sqrt(x)) if math.isfinite(x) else x


def RSQ(x):
    if fnan(x) or x < 0:
        return math.nan
    if x == 0:
        return math.copysign(math.inf, x)
    return f32(1.0 / math.sqrt(x)) if math.isfinite(x) else 0.0


def LOG2(x):
    if fnan(x) or x < 0:
        return math.nan
    if x == 0:
        return -math.inf
    return f32(math.log2(x)) if math.isfinite(x) else x


def EXP2(x):
    if fnan(x):
        return x
    try:
        return f32(2.0 ** x)
    except OverflowError:
        return math.inf


def ROUNDEVEN(x):
    return f32(float(round(x))) if math.isfinite(x) else x


def INT(x):  # F2I: NaN 0, saturating
    if isinstance(x, bool):
        return int(x)
    if isinstance(x, int):
        return x
    if fnan(x):
        return 0
    if x >= 2147483647:
        return 2147483647
    if x <= -2147483648:
        return -2147483648
    return int(x)


def UINT(x):
    if isinstance(x, float):
        if fnan(x) or x <= 0:
            return 0
        return min(int(x), 0xFFFFFFFF)
    return int(x) & 0xFFFFFFFF


def TEX(name, coords, swz):
    """A texel as a fixed function of the binding and the coordinates (their bits)."""
    key = name.encode() + b''.join(struct.pack('<I', d.bits(f32(float(c)))) for c in coords)
    h = zlib.crc32(key)
    vals = []
    for k in range(4):
        h = zlib.crc32(struct.pack('<I', h) + bytes([k]))
        vals.append(f32((h & 0xFFFF) / 65535.0))
    out = tuple(vals['xyzw'.index(c) if c in 'xyzw' else 'rgba'.index(c)] for c in swz)
    return out[0] if len(out) == 1 else out


def TSIZE(name, swz):
    h = zlib.crc32(name.encode())
    vals = [16 << (h % 5), 16 << ((h >> 3) % 5), 1 + (h >> 6) % 4, 1]
    out = tuple(vals['xyzw'.index(c) if c in 'xyzw' else 'rgba'.index(c)] for c in swz)
    return out[0] if len(out) == 1 else out


def conv(e):
    # texture(NAME, vecN(a, b, ...)).swz and textureSize(NAME, 0).swz before the generic conversion
    def tex(m):
        return f'TEX("{m.group(1)}", ({m.group(3)},), "{m.group(4) or "xyzw"}")'
    e = re.sub(r'\btexture\((\w+), vec([234])\(((?:[^()]|\([^()]*\)|\((?:[^()]|\([^()]*\))*\))*)\)\)(?:\.([xyzwrgba]+))?',
               tex, e)
    e = re.sub(r'\btextureSize\((\w+), 0\)\.([xyzwrgba]+)', lambda m: f'TSIZE("{m.group(1)}", "{m.group(2)}")', e)
    e = re.sub(r'\b(temp_\d+)\.([xyzw])\b', lambda m: f'{m.group(1)}[{"xyzw".index(m.group(2))}]', e)
    e = re.sub(r'(?<![\w.])(0x[0-9A-Fa-f]+|\d+)u\b', r'\1', e)
    e = _base_conv(e)
    e = re.sub(r'\bclamp\(', 'CLAMP(', e)
    e = re.sub(r'\bmin\(', 'MIN(', e)
    e = re.sub(r'\bmax\(', 'MAX(', e)
    e = re.sub(r'\binversesqrt\(', 'RSQ(', e)
    e = re.sub(r'\bsqrt\(', 'SQRT(', e)
    e = re.sub(r'\blog2\(', 'LOG2(', e)
    e = re.sub(r'\bexp2\(', 'EXP2(', e)
    e = re.sub(r'\broundEven\(', 'ROUNDEVEN(', e)
    e = re.sub(r'\bint_\(', 'INT(', e)
    e = re.sub(r'\buint_\(', 'UINT(', e)
    e = re.sub(r'^(F\(1\.0\)|[\w.\[\]]+) / ([\w.\[\]]+)$', r'DIV(\1, \2)', e.strip())
    return e


_base_conv = d.conv
d.conv = conv  # transpile() calls the module's conv


FRONT = [True]


class KeyedCB(dict):
    """Constant buffer words as a function of the trial's seed and the word's key (difftest_ps.RandomCB draws them in
    the order the program reads them, so two programs reading in another order saw other constants)."""
    def __init__(self, seed):
        super().__init__()
        self.seed = seed

    def __missing__(self, key):
        rng = random.Random(zlib.crc32(repr((self.seed, key)).encode()))
        if rng.random() < 0.5:
            v = d.intBitsToFloat(rng.randrange(16) | (0x80000000 if rng.random() < 0.5 else 0))
        else:
            v = f32(rng.uniform(-2, 2))
        self[key] = v
        return v


def run(py, inputs, cb):
    env = {'GLF': {'x': 10.5, 'y': 20.5, 'z': 0.5, 'w': 1.0}, 'CB': cb, 'IN': inputs, 'F': d.F, 'FMA': d.FMA,
           'Discard': d.Discard, 'INT': INT, 'UINT': UINT, 'floatBitsToInt': d.floatBitsToInt,
           'intBitsToFloat': d.intBitsToFloat, 'undef': 0,
           'floor': lambda x: f32(float(math.floor(x))) if math.isfinite(x) else x,
           'trunc': lambda x: f32(float(math.trunc(x))) if math.isfinite(x) else x,
           'ceil': lambda x: f32(float(math.ceil(x))) if math.isfinite(x) else x,
           'uintBitsToFloat': d.intBitsToFloat, 'floatBitsToUint': lambda x: d.bits(f32(x)), 'abs': abs,
           'MIN': MIN, 'MAX': MAX, 'CLAMP': CLAMP, 'DIV': DIV, 'SQRT': SQRT, 'RSQ': RSQ, 'LOG2': LOG2, 'EXP2': EXP2,
           'ROUNDEVEN': ROUNDEVEN, 'TEX': TEX, 'TSIZE': TSIZE, 'R': R, 'gl_FrontFacing': FRONT[0],
           'dFdx': lambda x: 0.0, 'dFdy': lambda x: 0.0, 'dFdxFine': lambda x: 0.0, 'dFdyFine': lambda x: 0.0,
           'support_buffer': types.SimpleNamespace(render_scale=[1.0] * 73),
           # 2026-10-20 (SA2's report): helpers other ports' decompiles call
           'isnan': lambda x: isinstance(x, float) and math.isnan(x),
           'TextureSizeUnscale': lambda size, index: size}
    exec(py, env)
    try:
        env['main']()
    except d.Discard:
        return 'discard'
    except ZeroDivisionError:
        # 2026-10-20 (SA2's report): a division the transpiler did not wrap in DIV (an operand that is not a plain
        # name); reported as its own outcome, so a mismatch shows when only one program gets there.
        return 'zerodiv'
    return tuple(d.bits(f32(env.get(f'out_attr0_{c}', 0.0))) for c in 'xyzw')


def prepare(path, defs):
    text = open(os.path.join(ORIG, path), encoding='utf-8').read()
    nl = text.index('\n') + 1
    text = text[:nl] + ''.join(f'#define SPIRV_CROSS_CONSTANT_ID_{k} {v}u\n' for k, v in defs) + text[nl:]
    code = d.decompile(text, 447)
    py = d.transpile(code)
    # every instruction's result is a float32 on the GPU: round each assignment (ints, bools and tuples pass through)
    py = re.sub(r'^(\s+)(\w+) = (.+)$', lambda m: f'{m.group(1)}{m.group(2)} = R({m.group(3)})'
                if not m.group(2) == 'global' else m.group(0), py, flags=re.M)
    return code, py


def R(x):
    if isinstance(x, float):
        return f32(x)
    if isinstance(x, tuple):
        return tuple(f32(v) if isinstance(v, float) else v for v in x)
    return x


def main():
    a, b = sys.argv[1], sys.argv[2]
    trials = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 300
    defs = [tuple(x.split('=')) for x in sys.argv[3:] if '=' in x]
    ca, pa = prepare(a, defs)
    cb_, pb = prepare(b, defs)
    print('statements', len(re.findall(r'^\s*\w+(?:\.[xyzw])? = ', ca, re.M)), '->',
          len(re.findall(r'^\s*\w+(?:\.[xyzw])? = ', cb_, re.M)))
    random.seed(11)
    mism = disc = 0
    for trial in range(trials):
        inputs = {(k, c): f32(random.uniform(-2, 2)) for k in range(32) for c in 'xyzw'}
        seed = random.randrange(1 << 30)
        ra = run(pa, inputs, KeyedCB(seed))
        rb = run(pb, inputs, KeyedCB(seed))
        disc += ra == 'discard'
        if ra != rb:
            mism += 1
            if mism <= 3:
                print('  trial', trial, ra, rb)
    print(f'{os.path.basename(a)}: {"OK" if not mism else "MISMATCH " + str(mism)} of {trials} ({disc} discarded)')
    return 1 if mism else 0


if __name__ == '__main__':
    sys.exit(main())
