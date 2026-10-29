#!/usr/bin/env python3
"""banjo-nx: "localized copies" of recompiled guest functions, the native replacements of app/src/cpu/banjo_hot_guest.cpp.

From sa2-nx's tools/localize_native.py (GPL-3.0, the same project family; sa2-nx took the idea from
UnleashedRecomp-NX's switch-localize.py), rewritten for Banjo's generated code and for a guard that needs no
hand-written list of the memory a function writes.

A copy runs the recompiled function's own statements (app/generated/renut_recomp.N.cpp, after every post-codegen step)
with these differences:
  - the guest registers the function keeps in the PPCContext (the volatile ones: r0, r3-r12, f0-f13, v0-v13, and
    whatever else it mentions as ctx.<register>) live in C++ locals: loaded at entry, written back at every return
    (only those the function can have changed) and synchronised around calls (below);
  - direct callees that are small enough come along as copies of their own, in the same header ("the closure"), so the
    compiler can inline them and a call costs no hook and no full register hand-over;
  - a call synchronises only what the callee can reach: before it, the registers this function may have changed that
    the callee (or anything it calls) mentions; after it, the registers the callee (or anything it calls) can have
    changed. A callee whose reach is unknown (an indirect call, a trap, a kernel import, a function an app hook
    replaces, a mid-asm hook) gets every register.

Each function is written three times into app/generated/hot/n_<id>.h:
  f_<name><Fast>     the copy that runs;
  f_<name><Logged>   the same text, every guest store first recorded in a write log (address, length, old bytes);
  o_<name>           the ORIGINAL statements (registers in ctx), stores recorded the same way.
The two logged forms are how the guard learns which guest memory a call writes without a list per function: it runs
both on copies of the registers, rolls the memory back from the log, and then runs the real copy and the real
recompiled function over a snapshot of exactly those bytes (banjo_hot_guest.cpp). A logged form stops at the first
call it cannot repeat (anything outside the closure): that call of the function is then not comparable.

What cannot be a copy (the tool says why and writes nothing): a function, or a closure member, with setjmp / longjmp,
an atomic compare-and-swap or a memory barrier (state shared with other threads: the guard's roll-back would be seen),
MMIO, the guest time base, a mid-asm hook, or a guest-memory write in a form the tool does not know.

HAND-WRITTEN NATIVES (app/src/cpu/hot/hand.txt, the code in app/src/cpu/hot/banjo_hand_natives.h). A function of
that list has a native version written by hand, banjo::hand::n_<id>::Native(ctx, base, s), which returns false when it
declines a call. Wherever this tool uses the generated copy f_<name> of such a function (as the function of a hook, or
as a callee inside another function's copy) the hand-written native is asked first and the generated copy runs only
when it declines. The original's logged form o_<name> is untouched: the guard still compares against the original's
statements. A hand-written native writes no register; the registers the original changes (this tool reads them from
its statements; hand.txt's exact= names those the native leaves as the original does) are that hook's "skip" set,
written as kSkip into every header whose copy contains the native: the guard does not compare them.

  python tools/localize_native.py --list app/src/cpu/hot/localized.txt      every function of the list (post_codegen.py)
  python tools/localize_native.py --list --bulk                             and the bulk list (post_codegen.py)

BULK (2026-10-22, bejeweled3-nx's method, docs/enhancements.md "Bulk localized copies"): app/src/cpu/hot/
localized_bulk.txt (written by tools/switch_hot_bulk.py from console profiles) lists hundreds more functions. Each is
made the same way; one the tool refuses is left out and named in the output. Their hooks are BANJO_HOT_HOOK_BULK lines
spread over app/generated/hot/banjo_hot_bulk_<k>.inc by the size of their headers, each compiled by its own
app/src/cpu/banjo_hot_shard_<k>.cpp, so the copies compile in parallel. One master key, banjo_cpu_native_bulk,
switches them all (each also has its own key).
  python tools/localize_native.py --survey sub_82949790 [more ...]         what a copy of each would look like
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'app')
GEN = os.path.join(APP, 'generated')
OUT = os.path.join(GEN, 'hot')
IMAGE = os.path.join(ROOT, 'out', 'renut_image.bin')
INDEX = os.path.join(ROOT, 'out', 'localize_index.json')
LIST = os.path.join(APP, 'src', 'cpu', 'hot', 'localized.txt')
BULK = os.path.join(APP, 'src', 'cpu', 'hot', 'localized_bulk.txt')
SHARDS = 12  # app/src/cpu/banjo_hot_shard_<k>.cpp (app/CMakeLists.txt lists them)
BULK_SLOT = 9999  # past PPCContext::host_counters: the guard's countdown is the hook's own (banjo_hot_guest.h)
HAND = os.path.join(APP, 'src', 'cpu', 'hot', 'hand.txt')
IMAGE_BASE = 0x82000000
INDEX_FORMAT = 8  # raise when summarise() changes: the cached index is rebuilt (5: carded vector stores; 6, 7: inline leaves; 8: r1 mirror, 2026-10-23)

REG = re.compile(r'\bctx\.(r\d+|f\d+|v\d+|cr\d|ctr|xer|reserved)\b')
DEF = re.compile(r'^DEFINE_REX_FUNC\((\w+)\) \{$')
# A call of the generated code's convention (group 1: the name as written), an indirect call, a trap.
CALL = re.compile(r'\b([A-Za-z_]\w*)\(ctx, base\);|\bREX_CALL_INDIRECT_FUNC(?:_AT)?\([^;]*\);|\bppc_trap\(ctx, base[^;]*\);')
PROMOTED = re.compile(r'\bREX_PROMOTED_CALL\(0x[0-9A-F]{8}, (\w+)\)')
LEAF_REG = re.compile(r'\bREX_LEAF_LOCAL\(\w+, l_\w+, (\w+)\)')
RETURN = re.compile(r'\breturn;')
LABEL = re.compile(r'^loc_[0-9A-F]{8}:\s*$')
GOTO = re.compile(r'\bgoto loc_[0-9A-F]{8};')
# Every name the generator writes in front of a parenthesis (tools/loop_locals.py's list): anything else is a
# port's own host function (a mid-asm hook), which a copy cannot contain.
NAME = re.compile(r'([A-Za-z_][A-Za-z0-9_:]*(?:<[A-Za-z0-9_ ]+>)?)\s*\(')
KNOWN = re.compile(r'^(?:REX_\w+|simde_\w+|__builtin_\w+|rex::ppc::\w+|std::fma|std::bit_cast<\w+>|double|float|sqrt|'
                   r'memset|if|for|switch|sizeof|u?int(?:8|16|32|64)_t|u(?:8|16|32|64)|size_t|int|short|char|'
                   r'static_cast<[A-Za-z0-9_ ]+>|compare(?:<\w+>)?|setFromMask|(?:en|dis)ableFlushMode(?:Unconditional)?|'
                   r'flushModeIs(?:On|Off)|ppc_trap|simde__m128i?)$')
# Forms that make a function unfit for a copy and for a closure (see the docstring).
SPECIAL = re.compile(r'\b(REX_GUEST_CAS|reserved|REX_MM_(?:STORE|LOAD)_\w+|ppc_setjmp|ppc_longjmp|REX_ENTER_GLOBAL_LOCK|'
                     r'REX_LEAVE_GLOBAL_LOCK|REX_CHECK_GLOBAL_LOCK|REX_QUERY_TIMEBASE|REX_UNIMPLEMENTED|'
                     r'std::atomic_thread_fence|storeFromGuest|loadFromHost|log2f)\b')
# Guest memory through a raw host address. Loads (the vector loads) and the two store forms the tool logs.
# banjo-nx (2026-10-23): the store-card form of a vector store too (package 34: REX_CARD(ea), then the store, one comma
# expression); the guard's Note goes in front of the whole expression, so the card and the store run together.
RAW_VECTOR_STORE = re.compile(r'^\t(?:REX_CARD\([^()]*\), )?simde_mm_store_si128\(\(simde__m128i\*\)REX_RAW_ADDR\(')
RAW_MEMSET = re.compile(r'^\tmemset\(\(void\*\)REX_RAW_ADDR\((.*)\), (\w+), (\d+)\);$')
RAW_LOAD = re.compile(r'^\tsimde_mm_store_si128\(\(simde__m128i\*\)\w+(?:\.\w+)+, .*REX_RAW_ADDR\(')
ALL = None  # "reach unknown": every register


def order(reg):
    return (re.sub(r'\d', '', reg), int(re.sub(r'\D', '', reg) or 0))


def type_of(reg):
    if reg.startswith('cr'):
        return 'PPCCRRegister'
    if reg == 'xer':
        return 'PPCXERRegister'
    if reg.startswith('v'):
        return 'PPCVRegister'
    return 'PPCRegister'


# tools/inline_memcalls.py wraps a constant-size memcpy / memset call as REX_MEMCPY_CONST(n, sub_X(ctx, base)); (the
# helper runs the call when it must). A localized copy keeps the plain call (its callees are reached through their
# own localized or hand-written forms, which the wrapper would hide), so the wrapper is taken off where the text is read.
MEMCALL_WRAP = re.compile(r'\bREX_MEM(?:CPY|SET)_CONST\(\d+, ((?:__imp__)?sub_[0-9A-F]{8}\(ctx, base\))\);')


# banjo-nx (2026-10-23): tools/inline_leaves.py gives each generated file static copies of tiny leaves of other files
# (inl_sub_X, defined in that file only); a copy made from the text calls the leaf by its own name instead, which this
# tool reaches as any callee (the copies' shard files cannot see another file's statics).
INLINE_LEAF = re.compile(r'\b(?:REX_INL\((sub_[0-9A-F]{8})\)|inl_(sub_[0-9A-F]{8}))\(ctx, base\)')


# banjo-nx (2026-10-23): tools/r1_local.py (cpu_aggressive) keeps r1 in a local mirror, r1l, synced to ctx.r1 by
# REX_R1_SYNC / KEEP / RELOAD / ASSUMED. This tool's register model knows ctx.r1 only: copies made from the mirror form
# left r1 out of their hand-overs and of the guard's register set, and three bulk copies wrote a wrong back chain in the
# emulator's guarded run. The text is read as before that pass (the copies keep their own local of r1).
R1_LOCAL_DECL = re.compile(r'^\s*REX_R1_LOCAL\(\);\s*$')
R1_SYNC = re.compile(r'\s*REX_R1_(?:SYNC|KEEP|RELOAD|ASSUMED)\(\);')
R1_MIRROR = re.compile(r'\br1l\b')


def plain_r1(line):
    if 'REX_R1_' not in line and 'r1l' not in line:
        return line
    if R1_LOCAL_DECL.match(line):
        return ''
    return R1_MIRROR.sub('ctx.r1', R1_SYNC.sub('', line))


def plain_calls(line):
    line = plain_r1(line)
    if 'REX_MEM' in line:
        line = MEMCALL_WRAP.sub(r'\1;', line)
    if 'REX_INL(' in line or 'inl_sub_' in line:  # the macro form (REX_INL(sub_X)) as the generated text has it
        line = INLINE_LEAF.sub(lambda m: (m.group(1) or m.group(2)) + '(ctx, base)', line)
    return line


def unleaf(lines):
    """A function's lines as they were before tools/leaf_locals.py: ctx.<register> again, without the declarations and
    the stores before the returns (from sa2-nx's tool). This tool then makes its own locals of them."""
    return [text for _, text in unleaf_numbered(list(enumerate(lines)))]


def unleaf_numbered(pairs):
    """unleaf() on (line number, text) pairs: the lines that stay keep their numbers."""
    text = '\n'.join(l for _, l in pairs)
    regs = LEAF_REG.findall(text)
    if not regs:
        return pairs
    names = re.compile(r'\bl_(%s)\b' % '|'.join(re.escape(r) for r in regs))
    out = []
    for n, l in pairs:
        # REX_LEAF_DEAD_R / _F (tools/leaf_dead_stores.py) are write-backs no caller reads: in the ctx form every
        # register is written where the function writes it, so they go like the stores.
        if re.match(r'\tREX_LEAF_LOCAL\(', l) or re.match(r'\t(?:REX_LEAF_(?:STORE|DEAD_R|DEAD_F)\(l_\w+, \w+\);\s?)+$', l):
            continue
        l = re.sub(r'\{ (?:REX_LEAF_(?:STORE|DEAD_R|DEAD_F)\(l_\w+, \w+\); )+return; \}', 'return;', l)
        out.append((n, names.sub(lambda m: 'ctx.' + m.group(1), l)))
    if 'REX_LEAF_' in '\n'.join(l for _, l in out):
        sys.exit('localize_native: a leaf-local form this tool does not know (tools/leaf_locals.py changed?)')
    return out


def written_registers(regs, text):
    """The registers a function can have changed (tools/leaf_locals.py's rule for r and f; vectors by their forms)."""
    written = []
    for r in regs:
        if r[0] in 'rf' and r[1:].isdigit():
            mentions = re.findall(r'\bctx\.' + r + r'\b(\.\w+)?(\s*(?:[-+*/|&^%]|<<|>>)?=(?!=))?', text)
            if all(field and not assign for field, assign in mentions) and not re.search(r'&\s*ctx\.' + r + r'\b', text):
                continue
        elif r[0] == 'v' and r[1:].isdigit():
            only_read = True
            for m in re.finditer(r'\bctx\.' + r + r'\b', text):
                before = text[max(0, m.start() - 48):m.start()]
                after = text[m.end():m.end() + 80]
                if re.search(r'simde_mm_load_(?:si128\(\(simde__m128i\*\)|ps\()$', before):
                    continue  # a vector load of the register
                a = re.match(r'\.\w+\[[^\]]*\](\s*(?:[-+*/|&^%]|<<|>>)?=(?!=))?', after)
                if a and not a.group(1):
                    continue  # one element read
                only_read = False
                break
            if only_read:
                continue
        written.append(r)
    return written


def summarise(name, lines):
    """What the index keeps of a function (its code lines without comments, in ctx form)."""
    text = '\n'.join(lines)
    regs = sorted({m.group(1) for m in REG.finditer(text)}, key=order)
    callees = []
    unknown = 0
    traps = 0
    for l in lines:
        for m in CALL.finditer(l):
            if m.group(1) is None:
                if m.group(0).startswith('ppc_trap('):
                    traps += 1
                else:
                    unknown += 1
            elif m.group(1) not in callees:
                callees.append(m.group(1))
    if traps:
        # The trap helper (the generated pch) reads r3 and r4 of the context for its message and writes nothing.
        regs = sorted(set(regs) | {'r3', 'r4'}, key=order)
    special = sorted({m.group(1) for m in SPECIAL.finditer(text)})
    for l in lines:
        for m in NAME.finditer(l):
            n = m.group(1)
            if KNOWN.match(n) or l[m.end():].startswith('ctx, base)'):
                continue
            special.append('host function ' + n)
        if 'REX_RAW_ADDR(' in l and not (RAW_VECTOR_STORE.match(l) or RAW_MEMSET.match(l) or RAW_LOAD.match(l)):
            special.append('raw guest access')
    return {'regs': regs, 'written': written_registers(regs, text), 'callees': callees, 'unknown': unknown,
            'traps': traps, 'special': sorted(set(special)), 'lines': len(lines)}


class Index:
    """Every generated function: where its body is and its summary (cached by the files' modification times)."""

    def __init__(self, gen):
        self.gen = gen
        self.files = {}
        paths = sorted(glob.glob(os.path.join(gen, '*_recomp.*.cpp')))
        if not paths:
            sys.exit('localize_native: no *_recomp.N.cpp in ' + gen)
        stamp = [[os.path.basename(p), os.path.getmtime(p), os.path.getsize(p)] for p in paths]
        me = INDEX_FORMAT
        try:
            cached = json.load(open(INDEX, encoding='utf-8'))
            if cached.get('stamp') == stamp and cached.get('gen') == os.path.abspath(gen) and cached.get('tool') == me:
                self.funcs = cached['funcs']
                return
        except (OSError, ValueError):
            pass
        self.funcs = {}
        for p in paths:
            lines = open(p, encoding='utf-8', errors='replace').read().split('\n')
            i = 0
            while i < len(lines):
                m = DEF.match(lines[i])
                if not m:
                    i += 1
                    continue
                j = i + 1
                while j < len(lines) and lines[j] != '}':
                    j += 1
                code = unleaf([plain_calls(l) for l in lines[i + 1:j] if not l.lstrip().startswith('//')])
                s = summarise(m.group(1), code)
                s['file'] = os.path.basename(p)
                s['at'] = [i + 1, j]
                self.funcs[m.group(1)] = s
                i = j + 1
        os.makedirs(os.path.dirname(INDEX), exist_ok=True)
        json.dump({'gen': os.path.abspath(gen), 'stamp': stamp, 'tool': me, 'funcs': self.funcs},
                  open(INDEX, 'w', encoding='utf-8'))

    def body(self, name):
        f = self.funcs[name]
        if f['file'] not in self.files:
            self.files[f['file']] = open(os.path.join(self.gen, f['file']), encoding='utf-8',
                                         errors='replace').read().split('\n')
        a, b = f['at']
        lines = self.files[f['file']]
        # (1-based line number in the generated file, text), comments left out.
        return unleaf_numbered([(k + 1, plain_calls(lines[k])) for k in range(a, b)
                                if not lines[k].lstrip().startswith('//')])


def strip_comments(text, toml=False):
    if toml:
        return re.sub(r'#[^\n]*', ' ', text)
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def list_entries(path=LIST):
    """The functions of app/src/cpu/hot/localized.txt: [name, options, what]."""
    out = []
    if not os.path.isfile(path):
        return out
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split()
        name = parts[0] if not re.fullmatch(r'[0-9A-Fa-f]{8}', parts[0]) else 'sub_' + parts[0].upper()
        opts = {}
        k = 1
        while k < len(parts) and re.fullmatch(r'[a-z_]+(=\S+)?', parts[k]) and parts[k].split('=')[0] in (
                'off', 'key', 'closure', 'member', 'inline'):
            key, _, value = parts[k].partition('=')
            opts[key] = value or True
            k += 1
        out.append([name, opts, ' '.join(parts[k:])])
    return out


def hand_entries(path=HAND):
    """The hand-written natives of app/src/cpu/hot/hand.txt: {generated name: {'id': namespace suffix, 'exact': set}}."""
    out = {}
    if not os.path.isfile(path):
        return out
    for line in open(path, encoding='utf-8'):
        line = line.split('#')[0].strip()
        if not line:
            continue
        parts = line.split()
        name = parts[0] if not re.fullmatch(r'[0-9A-Fa-f]{8}', parts[0]) else 'sub_' + parts[0].upper()
        entry = {'id': identifier(name), 'exact': set()}
        for part in parts[1:]:
            key, _, value = part.partition('=')
            if key == 'exact':
                entry['exact'] = set(value.split(','))
            elif key == 'id':
                entry['id'] = value
            else:
                sys.exit(f'localize_native: hand.txt: {part!r} in the line of {name}')
        out[name] = entry
    return out


def skip_literal(regs):
    """The guard's RegSkip of banjo_hot_common.h for a set of register names."""
    gpr = fpr = cr = 0
    vr = [0, 0]
    ctr = xer = False
    for r in regs:
        if r == 'ctr':
            ctr = True
        elif r == 'xer':
            xer = True
        elif r.startswith('cr'):
            cr |= 1 << int(r[2:])
        elif r.startswith('r') and r[1:].isdigit():
            gpr |= 1 << int(r[1:])
        elif r.startswith('f'):
            fpr |= 1 << int(r[1:])
        elif r.startswith('v'):
            n = int(r[1:])
            vr[n >> 6] |= 1 << (n & 63)
        else:
            sys.exit(f'localize_native: no skip bit for register {r}')
    return ('{0x%08Xu, 0x%08Xu, {0x%016Xull, 0x%016Xull}, 0x%02Xu, %s, %s}'
            % (gpr, fpr, vr[0], vr[1], cr, 'true' if ctr else 'false', 'true' if xer else 'false'))


class Hooks:
    """What an app or SDK hook may replace: every 82xxxxxx address and every identifier their sources' code mentions
    (comments and __imp__ references left out, as in sa2-nx's tool: a comment hooks nothing and __imp__sub_X is the
    original). The functions of localized.txt are not in it: a copy touches no more than its original."""

    def __init__(self, natives):
        self.addresses = set()
        self.words = set()
        roots = [os.path.join(APP, 'src'), os.path.join(ROOT, 'sdk', 'src'), os.path.join(ROOT, 'sdk', 'include')]
        tomls = [os.path.join(APP, 'config', 'renut_hooks.toml'), os.path.join(APP, 'config', 'renut_gpu_funcs.toml'),
                 os.path.join(APP, 'renut_config.toml'), os.path.join(APP, 'renut_manifest.toml')]
        paths = [(p, True) for p in tomls if os.path.isfile(p)]
        for root in roots:
            for d, _, fs in os.walk(root):
                if os.path.abspath(d).startswith(os.path.abspath(os.path.join(APP, 'src', 'cpu', 'hot'))):
                    continue  # the list of the natives themselves
                for fn in fs:
                    if fn.endswith(('.cpp', '.h', '.inc', '.hpp', '.c')):
                        paths.append((os.path.join(d, fn), False))
        for path, toml in paths:
            try:
                text = open(path, encoding='utf-8', errors='ignore').read()
            except OSError:
                continue
            text = strip_comments(text, toml)
            text = re.sub(r'__imp__\w+', ' ', text)
            self.addresses.update(m.upper() for m in re.findall(r'(?<![0-9A-Fa-f])(82[0-9A-Fa-f]{6})(?![0-9A-Fa-f])', text))
            if not toml:
                self.words.update(re.findall(r'\b[A-Za-z_]\w{5,}\b', text))
        self.natives = set(natives)

    def replaced(self, name):
        if name in self.natives:
            return False
        if re.fullmatch(r'sub_[0-9A-F]{8}', name):
            return name[4:] in self.addresses
        return name in self.words


class Reach:
    """For a call: the registers the callee and everything under it mention, and those they can change (strongly
    connected components over the call graph, on demand; sa2-nx's). ALL = unknown."""

    def __init__(self, index, hooks):
        self.funcs = index.funcs
        self.hooks = hooks
        self.memo = {}

    def own(self, name):
        imp = name.startswith('__imp__')
        base = name[7:] if imp else name
        f = self.funcs.get(base)
        if f is None or f['unknown'] or (not imp and self.hooks.replaced(base)):
            return ALL, ALL, ()
        if any(s.startswith('host function') or 'setjmp' in s or 'longjmp' in s for s in f['special']):
            return ALL, ALL, ()
        return set(f['regs']), set(f['written']), list(f['callees'])

    def of(self, root):
        if root in self.memo:
            return self.memo[root]
        index, low, on_stack, stack, owns = {}, {}, set(), [], {}
        counter = 0
        frames = []

        def push(n):
            nonlocal counter
            index[n] = low[n] = counter
            counter += 1
            stack.append(n)
            on_stack.add(n)
            owns[n] = self.own(n)
            frames.append([n, owns[n][2], 0])

        push(root)
        while frames:
            frame = frames[-1]
            n, callees, pos = frame
            if pos < len(callees):
                frame[2] += 1
                c = callees[pos]
                if c in self.memo:
                    continue
                if c not in index:
                    push(c)
                elif c in on_stack:
                    low[n] = min(low[n], index[c])
                continue
            frames.pop()
            if frames:
                parent = frames[-1][0]
                low[parent] = min(low[parent], low[n])
            if low[n] != index[n]:
                continue
            members = []
            while True:
                w = stack.pop()
                on_stack.discard(w)
                members.append(w)
                if w == n:
                    break
            member_set = set(members)
            mention, change = set(), set()
            for w in members:
                regs, wr, callees = owns[w]
                if regs is ALL:
                    mention = ALL
                    break
                mention |= regs
                change |= wr
                for c in callees:
                    if c in member_set:
                        continue
                    rc = self.memo.get(c, (ALL, ALL))
                    if rc[0] is ALL:
                        mention = ALL
                        break
                    mention |= rc[0]
                    change |= rc[1]
                if mention is ALL:
                    break
            for w in members:
                self.memo[w] = (ALL, ALL) if mention is ALL else (mention, change)
        return self.memo[root]


ONLY_GOTO = re.compile(r'^\tgoto (loc_[0-9A-F]{8});$')
ANY_GOTO = re.compile(r'\bgoto (loc_[0-9A-F]{8});')
ONLY_RETURN = re.compile(r'^\t+(?:return;|\{ return; \})$')
# A statement that is nothing but a call (or the two alternative calls of a promoted indirect call).
ONLY_CALL = re.compile(r'^\t+(?:[A-Za-z_]\w*\(ctx, base\);|REX_CALL_INDIRECT_FUNC(?:_AT)?\([^;]*\);|'
                       r'ppc_trap\(ctx, base[^;]*\);)$|^\tif \(REX_PROMOTED_IS\(')
SWITCH_LINE = re.compile(r'^\t(?:case \d+:|default:)$|^\t\t(?:goto loc_[0-9A-F]{8};|__builtin_trap\(\);.*)$')


def line_writes(line):
    """The context registers one statement (in ctx form) can change. Everything that is not plainly a read counts."""
    if 'REX_LOOP_IN(' in line or 'REX_LOOP_LOCAL(' in line:
        return set()  # tools/loop_locals.py: a loop local takes the register's value
    out = set()
    for m in REG.finditer(line):
        r = m.group(1)
        before = line[max(0, m.start() - 48):m.start()]
        after = line[m.end():m.end() + 80]
        if r[0] in 'rf' and r[1:].isdigit():
            a = re.match(r'(\.\w+)?(\[[^\]]*\])?(\s*(?:[-+*/|&^%]|<<|>>)?=(?!=))?', after)
            if a.group(1) and not a.group(3) and not re.search(r'&\s*$', before):
                continue  # a field read
        elif r[0] == 'v' and r[1:].isdigit():
            if re.search(r'simde_mm_load_(?:si128\(\(simde__m128i\*\)|ps\()$', before):
                continue  # a vector load of the register
            a = re.match(r'\.\w+\[[^\]]*\](\s*(?:[-+*/|&^%]|<<|>>)?=(?!=))?', after)
            if a and not a.group(1):
                continue  # one element read
        out.add(r)
    return out


def dirty_sets(name, lines, call_mentions):
    """For every statement: the registers whose local can differ from the context when the statement starts (changed
    on some path since the function's entry or since the last call that stored them). A return stores only those, a
    call only those its callee can reach: the path that changes two registers does not pay for the forty another
    path changes. call_mentions(line) -> the reach of each call on the line (ALL = everything).
    Returns ([set per line, None where the line is never reached], writes per line, the set at the function's end)."""
    n = len(lines)
    label_at = {}
    for i, l in enumerate(lines):
        if LABEL.match(l):
            label_at[l.strip()[:-1]] = i
    succ = [None] * n
    writes = [set() for _ in range(n)]
    i = 0
    while i < n:
        l = lines[i]
        if l.startswith('\tswitch ('):
            j = i + 1
            targets = set()
            while j < n and lines[j] != '\t}':
                m = ANY_GOTO.search(lines[j])
                if not SWITCH_LINE.match(lines[j]):
                    sys.exit(f'localize_native: {name}: a switch body line this tool does not know: {lines[j].strip()}')
                if m:
                    targets.add(label_at[m.group(1)])
                j += 1
            succ[i] = sorted(targets)  # a jump table: every case jumps, the default traps
            writes[i] = line_writes(l)
            i = j + 1
            continue
        m = ONLY_GOTO.match(l)
        if m:
            succ[i] = [label_at[m.group(1)]]
        elif ONLY_RETURN.match(l):
            succ[i] = []
        else:
            succ[i] = [i + 1] + [label_at[g.group(1)] for g in ANY_GOTO.finditer(l)]
        writes[i] = line_writes(l)
        i += 1
    dirty = [None] * (n + 1)  # n: falling off the end
    dirty[0] = frozenset()
    work = [0]
    while work:
        i = work.pop()
        if i >= n or succ[i] is None:
            continue
        d = set(dirty[i]) | writes[i]
        mentions = call_mentions(lines[i])
        if mentions and ONLY_CALL.match(lines[i]):
            # The call stores what its callee can reach (and loads back what the callee can change, a part of it):
            # those locals equal the context again. Two alternative calls: only what both store.
            stored = None
            for mention in mentions:
                s = set(d) if mention is ALL else (d & mention)
                stored = s if stored is None else (stored & s)
            d -= stored
        for j in succ[i]:
            merged = frozenset(d) if dirty[j] is None else (dirty[j] | d)
            if merged != dirty[j]:
                dirty[j] = merged
                work.append(j)
    return dirty[:n], writes, dirty[n]


def base_name(written):
    return written[7:] if written.startswith('__imp__') else written


def fits(index, hooks, name):
    """Why a function cannot be in a closure (or be a copy's root), or None."""
    f = index.funcs.get(name)
    if f is None:
        return 'not a generated function'
    if f['special']:
        return ', '.join(f['special'])
    return None


def closed_tree(index, hooks, name, limit, memo):
    """The functions under `name` (itself included) when every one of them can be a copy and none makes a call that
    cannot be repeated (indirect, trap aside, import, hooked); else a string saying why not. `name` as a base name."""
    if name in memo:
        return memo[name]
    seen = []
    stack = [name]
    why = None
    lines = 0
    while stack and why is None:
        n = stack.pop()
        if n in seen:
            continue
        f = index.funcs.get(n)
        why = fits(index, hooks, n)
        if why is None and f['unknown']:
            why = 'it (or %s under it) makes an indirect call' % n if n != name else 'it makes an indirect call'
        if why is not None:
            if n != name and not why.startswith('it (or'):
                why = '%s under it: %s' % (n, why)
            break
        seen.append(n)
        lines += f['lines']
        if lines > limit:
            why = 'its call tree is over %d lines' % limit
            break
        for written in f['callees']:
            b = base_name(written)
            if b not in index.funcs:
                why = 'it (or %s under it) calls %s' % (n, written)
                break
            if not written.startswith('__imp__') and hooks.replaced(b):
                why = 'an app hook may replace %s' % b
                break
            stack.append(b)
    memo[name] = why if why is not None else seen
    return memo[name]


def closure_of(index, hooks, root, max_lines, max_member):
    """The root and the direct callees that come along as copies: those whose whole call tree can be repeated by the
    guard and is small (a copy is for the hot path; the cold subsystems a function reaches stay real calls). A call
    of the root to itself is a call to its own copy. Returns (members in order, {name as written: why it stays a
    real call})."""
    members = [root]
    left_out = {}
    total = index.funcs[root]['lines']
    memo = {}
    for written in index.funcs[root]['callees']:
        b = base_name(written)
        if b in members:
            continue
        why = None
        if not written.startswith('__imp__') and hooks.replaced(b):
            why = 'an app hook may replace it'
        tree = None
        if why is None:
            tree = closed_tree(index, hooks, b, max_member, memo)
            if isinstance(tree, str):
                why = tree
            elif root in tree:
                why = 'it calls back'
        if why is None:
            extra = [m for m in tree if m not in members]
            size = sum(index.funcs[m]['lines'] for m in extra)
            if total + size > max_lines:
                why = 'the closure is full'
            else:
                members.extend(extra)
                total += size
        if why is not None:
            left_out[written] = why
    return members, left_out


def shown(path):
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:  # another drive
        return path


def fnv1a64(data, h=0xCBF29CE484222325):
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


_ADDRESSES = None


def addresses(gen):
    """name -> (guest address, bytes up to the next function) from the generated function table."""
    global _ADDRESSES
    if _ADDRESSES is None:
        text = open(os.path.join(gen, 'renut_init.cpp'), encoding='utf-8').read()
        table = [(int(a, 16), n) for a, n in re.findall(r'^\t\{ 0x([0-9A-F]{8}), (\w+) \},$', text, re.M)]
        table.sort()
        _ADDRESSES = {}
        for k, (a, n) in enumerate(table):
            end = table[k + 1][0] if k + 1 < len(table) else a + 4
            _ADDRESSES[n] = (a, end - a)
    return _ADDRESSES


def identifier(name):
    return name[4:] if re.fullmatch(r'sub_[0-9A-F]{8}', name) else name


# tools/vec_unaligned_stores.py writes stvlx / stvrx as REX_STVLX / REX_STVRX (one helper call each, not a REX_STORE_
# form). banjo-nx (2026-10-22): the logged forms record the bytes they cover (stvlx: ea up to the next 16-byte
# boundary; stvrx: from the boundary below ea up to ea, nothing when ea is aligned). Before, the guard neither rolled
# them back nor compared them: a guarded call made those stores four times, and sub_82226330's guard (bulk copies,
# emulator) reported a difference that repeated (its first callee writes a matrix with them). The Fast form keeps the
# helper: its Note is a constant true.
# banjo-nx (2026-10-23): with the store-card form too (REX_STVLX_C / REX_STVRX_C): without it the logged forms missed
# them again and sub_82226330's guard reported its old difference (batch 5's emulator run).
VEC_LR = re.compile(r'^(\t+)REX_STV([LR])X(_C)?\(ea, ([\w.\[\]]+)\);$')


def store_line(l):
    """A line with its guest stores recorded first (the Fast policy's Note is a constant true)."""
    stores = False
    if 'REX_STORE_' in l:
        l = l.replace('REX_STORE_', 'BANJO_HOT_STORE_')
        stores = True
    m = VEC_LR.match(l)
    if m:
        span = ('(u32)(ea), 16u - ((u32)(ea) & 0xFu)' if m.group(2) == 'L'
                else '(u32)(ea) & ~0xFu, (u32)(ea) & 0xFu')
        l = '%sif (s.Note(%s)) REX_STV%sX%s(ea, %s);' % (m.group(1), span, m.group(2), m.group(3) or '', m.group(4))
        stores = True
    elif any(f in l for f in ('REX_STVLX(', 'REX_STVRX(', 'REX_STVLX_C(', 'REX_STVRX_C(')):
        sys.exit('localize_native: an unaligned vector store in a form this tool does not record: ' + l.strip()[:120])
    if RAW_VECTOR_STORE.match(l):
        at = l.index('REX_RAW_ADDR(') + len('REX_RAW_ADDR(')
        depth, k = 1, at
        while depth:
            depth += {'(': 1, ')': -1}.get(l[k], 0)
            k += 1
        l = '\tif (s.Note((u32)(%s), 16)) %s' % (l[at:k - 1], l.lstrip('\t'))
        stores = True
    m = RAW_MEMSET.match(l)
    if m:
        l = '\tif (s.Note((u32)(%s), %s)) %s' % (m.group(1), m.group(3), l.lstrip('\t'))
        stores = True
    return l, stores


def localize(index, hooks, reach, root, what, opts, out_dir, quiet=False, hand=None):
    funcs = index.funcs
    hand = hand or {}
    if root not in funcs:
        sys.exit(f'localize_native: {root} is not a generated function')
    why = fits(index, hooks, root)
    if why:
        sys.exit(f'localize_native: {root} cannot be a copy: {why}')
    if hooks.replaced(root):
        sys.exit(f'localize_native: {root} is mentioned by an app or SDK source: a hook may replace it already')
    max_lines = int(opts.get('closure', 6000))
    members, left_out = closure_of(index, hooks, root, max_lines, int(opts.get('member', 1200)))
    member_set = set(members)
    externs = set()
    stats = {'calls_closed': 0, 'calls_open': 0, 'synced': []}

    def call_reach(m):
        """(what the callee and everything under it mention, what they can change, is it a copy of this header)."""
        written_name = m.group(1)
        b = base_name(written_name) if written_name else None
        if b in member_set:
            return reach.of(b) + (True,)
        if written_name is not None:
            return reach.of(written_name) + (False,)
        if m.group(0).startswith('ppc_trap('):
            return {'r3', 'r4'}, set(), False  # the trap helper reads two registers and writes none
        return ALL, ALL, False

    def call_text(m, me, my_regs, my_written, localized):
        """The replacement of one call statement. my_written: the registers that can be dirty here."""
        written_name = m.group(1)
        b = base_name(written_name) if written_name else None
        mention, change, closed = call_reach(m)
        if not closed and written_name:
            externs.add(written_name)
        if not localized:
            if closed:
                return '{ o_%s(ctx, base, s); if (s.Stopped()) return; }' % b
            return '{ s.Open(); return; }'
        out_regs = my_written if mention is ALL else [r for r in my_written if r in mention]
        in_regs = my_regs if change is ALL else [r for r in my_regs if r in change]
        before = ''.join(f'ctx.{r} = l_{r}; ' for r in out_regs)
        after = ''.join(f' l_{r} = ctx.{r};' for r in in_regs)
        stats['synced'].append((len(out_regs), len(in_regs)))
        if closed:
            stats['calls_closed'] += 1
            if b in hand:
                # The hand-written native first; the generated copy of the callee when it declines. The native
                # changes no register, so nothing is loaded back after it.
                return ('{ ' + before + 'if (!::banjo::hand::n_%s::Native(ctx, base, s)) { f_%s(ctx, base, s); '
                        'if (s.Stopped()) return;%s } else if (s.Stopped()) return; }' % (hand[b]['id'], b, after))
            return '{ ' + before + 'f_%s(ctx, base, s); if (s.Stopped()) return;' % b + after + ' }'
        stats['calls_open'] += 1
        return '{ if (s.Open()) return; ' + before + m.group(0) + after + ' }'

    # Callee copies the compiler must inline into their callers: then a guest call inside the copy is no call at all,
    # the registers handed over through the context are forwarded (the context is `restrict`: the store before the
    # call and the load in the callee are the same value) and constant arguments reach the callee's code. Not a
    # function that can call itself, and not a large one with several call sites (the copy would only grow).
    calls_to = {m: 0 for m in members}
    callees_of = {}
    for name in members:
        callees_of[name] = set()
        for l in (text for _, text in index.body(name)):
            for m in CALL.finditer(l):
                b = base_name(m.group(1)) if m.group(1) else None
                if b in member_set:
                    calls_to[b] += 1
                    callees_of[name].add(b)

    def reaches_itself(name):
        seen, stack = set(), list(callees_of[name])
        while stack:
            n = stack.pop()
            if n == name:
                return True
            if n not in seen:
                seen.add(n)
                stack.extend(callees_of[n])
        return False

    inline_limit = int(opts.get('inline', 300))
    forced = {m for m in members[1:] if not reaches_itself(m) and
              (funcs[m]['lines'] <= inline_limit or calls_to[m] == 1) and inline_limit > 0}
    # The generated copy of a function with a hand-written native only runs when the native declines: out of line and
    # cold (inlined, it took registers away from the hand-written path: every call then saved five pairs of them).
    forced -= set(hand)

    declarations = []
    # Blocks: straight runs of statements. A block starts at a function's first statement, at every label and after
    # every statement that can leave (a jump, a return, the end of a switch). The Counting policy counts their
    # entries; the map says which lines of this header and of the generated file each block holds.
    blocks = 0
    entry_block = {}
    block_of_line = []  # (text, block or None) for every line of the function bodies, in order
    original_lines = {}
    for name in members:
        numbered = index.body(name)
        lines = [l for _, l in numbered]
        text = '\n'.join(lines)
        regs = funcs[name]['regs']
        written = funcs[name]['written']
        for m in PROMOTED.finditer(text):
            externs.add(m.group(1))
        dirty, writes, dirty_end = dirty_sets(name, lines, lambda l: [call_reach(m)[0] for m in CALL.finditer(l)])
        file_lines = original_lines.setdefault(funcs[name]['file'], {})
        for localized in (True, False):
            body = []  # (text, block)
            block = None
            if localized:
                block = blocks
                blocks += 1
                entry_block[name] = block
                # Everything of the function in the generated file that is not a statement below (the leaf-local
                # declarations and final stores, the prologue) counts with its first block.
                for k in range(funcs[name]['at'][0] - 1, funcs[name]['at'][1] + 1):
                    file_lines[k + 1] = block
                body.append((f'\ts.Hit({block});', block))
            pending = False
            in_switch = False
            for at, (number, l) in enumerate(numbered):
                # What can be dirty when this statement's call or return runs (an unreached statement: everything).
                here = sorted(written if dirty[at] is None else (dirty[at] | writes[at]), key=order)
                sync_out = ''.join(f'ctx.{r} = l_{r}; ' for r in here)
                if localized:
                    if LABEL.match(l):
                        block = blocks
                        blocks += 1
                        pending = False
                        body.append((l, block))
                        body.append((f'\ts.Hit({block});', block))
                        file_lines[number] = block
                        continue
                    if pending:
                        block = blocks
                        blocks += 1
                        pending = False
                        body.append((f'\ts.Hit({block});', block))
                    file_lines[number] = block
                    if l.startswith('\tswitch ('):
                        in_switch = True
                    elif in_switch and l == '\t}':
                        in_switch = False
                        pending = True
                    if not in_switch and (GOTO.search(l) or RETURN.search(l)):
                        pending = True
                    l = REG.sub(lambda m: 'l_' + m.group(1), l)
                l, stores = store_line(l)
                l = CALL.sub(lambda m: call_text(m, name, regs, here, localized), l)
                if localized and sync_out:
                    # Braces: the generated code also writes a return as the body of a one-line `if`.
                    l = RETURN.sub(lambda m: m.group(0) if 'if (s.' in l[max(0, m.start() - 24):m.start()]
                                   else '{ ' + sync_out + 'return; }', l)
                body.append((l, block))
                if stores:
                    body.append(('\tif (s.Stopped()) return;', block))
            if localized:
                how = ('[[gnu::always_inline]] static inline' if name in forced
                       else '[[gnu::noinline, gnu::cold]] static' if name in hand else 'static')
                head = (f'template <class S>\n{how} '
                        f'void f_{name}(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s)')
                declarations.append(head + ';')
                block_of_line.append((head.split('\n')[0], None))
                block_of_line.append((head.split('\n')[1] + ' {', entry_block[name]))
                for r in regs:
                    block_of_line.append((f'\t{type_of(r)} l_{r} = ctx.{r};', entry_block[name]))
                block_of_line.extend(body)
                end = sorted(written if dirty_end is None else dirty_end, key=order)
                if end and not (numbered and ONLY_RETURN.match(numbered[-1][1])):
                    block_of_line.append(('\t' + ' '.join(f'ctx.{r} = l_{r};' for r in end), block))
                block_of_line.append(('}', block))
                block_of_line.append(('', None))
            else:
                head = (f'static void o_{name}(PPCContext& __restrict ctx, uint8_t* base, '
                        f'[[maybe_unused]] ::banjo::hot::Logged& s)')
                declarations.append(head + ';')
                block_of_line.append((head + ' {', None))
                block_of_line.extend((l, None) for l, _ in body)
                block_of_line.append(('}', None))
                block_of_line.append(('', None))

    addr = addresses(index.gen)
    image = open(IMAGE, 'rb').read() if os.path.isfile(IMAGE) else None
    if image is None:
        sys.exit('localize_native: out/renut_image.bin is missing (the code generator writes it: tools\\pc_env.bat)')
    ranges = []
    h = 0xCBF29CE484222325
    for name in members:
        a, n = addr[name]
        ranges.append((a, n))
        h = fnv1a64(image[a - IMAGE_BASE:a - IMAGE_BASE + n], h)
    rid = identifier(root)
    callees = [m for m in members[1:]]
    # The registers the hand-written natives in this header leave as they found them where the original changes them.
    hand_members = [m for m in members if m in hand]
    skip = set()
    for m in hand_members:
        skip |= set(funcs[m]['written']) - hand[m]['exact']
    entry = (f'if (!::banjo::hand::n_{hand[root]["id"]}::Native(ctx, base, s)) f_{root}(ctx, base, s);'
             if root in hand else f'f_{root}(ctx, base, s);')
    hand_note = ('' if not hand_members else
                 '\n// Hand-written natives in front of their copies (app/src/cpu/hot/banjo_hand_natives.h): '
                 + ', '.join(hand_members) + '.\n// Registers the guard does not compare for this hook (they leave '
                 'them as they found them): ' + (', '.join(sorted(skip, key=order)) or 'none') + '.')
    left = ''.join(f'\n//   {k}: {v}' for k, v in sorted(left_out.items()))
    synced = ', '.join(f'{a}/{b}' for a, b in stats['synced'])
    top = f'''// {root}: {what or "localized copy"}.
// GENERATED by tools/localize_native.py from app/generated/{funcs[root]["file"]} (do not edit; tools/post_codegen.py
// writes it again after every codegen). The recompiled function's own statements with its {len(funcs[root]["regs"])} context registers
// in C++ locals ({len(funcs[root]["written"])} of them written back at a return), {len(callees)} callee(s) as copies of their own{": " + ", ".join(callees) if callees else ""}.
// Calls: {stats["calls_closed"]} to copies, {stats["calls_open"]} real (registers stored before / loaded after each: {synced or "-"}).
// Real calls, which a guarded call cannot repeat:{left or " none"}
// f_*<Fast> runs; f_*<Logged> and o_* (the original statements) record their guest stores for the guard
// (app/src/cpu/banjo_hot_guest.cpp); f_*<Counting> counts block entries for tools/hot_arm_compare.py.{hand_note}
// Mechanism from sa2-nx (GPL-3.0).
#pragma once

{"".join(f"REX_EXTERN({c});" + chr(10) for c in sorted(externs))}
namespace banjo::hot::n_{rid} {{

{chr(10).join(declarations)}
'''.split('\n')
    bottom = f'''// The guest code these copies were made from (first call: the hook stays off unless it is loaded unchanged).
inline constexpr ::banjo::hot::CodeRange kCode[] = {{{", ".join(f"{{0x{a:08X}u, {n}u}}" for a, n in ranges)}}};
inline constexpr uint64_t kCodeHash = 0x{h:016X}ull;
inline std::atomic<uint64_t> g_hits[{blocks}];  // banjo_hot_count
inline constexpr ::banjo::hot::RegSkip kSkip = {skip_literal(skip)};

[[gnu::noinline]] inline void Native(PPCContext& ctx, uint8_t* base) {{
  ::banjo::hot::Fast s;
  {entry}
}}
inline void NativeLogged(PPCContext& ctx, uint8_t* base, ::banjo::hot::Logged& s) {{ {entry} }}
inline void ReferenceLogged(PPCContext& ctx, uint8_t* base, ::banjo::hot::Logged& s) {{ o_{root}(ctx, base, s); }}
inline void NativeCounted(PPCContext& ctx, uint8_t* base) {{
  ::banjo::hot::Counting s{{g_hits}};
  {entry}
}}

}}  // namespace banjo::hot::n_{rid}
'''.split('\n')
    header_lines = {}
    for k, (_, block) in enumerate(block_of_line):
        if block is not None:
            header_lines[len(top) + k + 1] = block
    for k, l in enumerate(bottom):  # the entry function: with the root's first block
        if l.startswith('[[gnu::noinline]] inline void Native('):
            for j in range(4):
                header_lines[len(top) + len(block_of_line) + k + j + 1] = entry_block[root]
    header = '\n'.join(top + [l for l, _ in block_of_line] + bottom)
    path = os.path.join(out_dir, f'n_{rid}.h')
    os.makedirs(out_dir, exist_ok=True)
    try:
        same = open(path, encoding='utf-8').read() == header
    except OSError:
        same = False
    if not same:
        open(path, 'w', encoding='utf-8', newline='\n').write(header)
    json.dump({'root': root, 'id': rid, 'members': members, 'blocks': blocks, 'entry': entry_block,
               'header': header_lines, 'original': original_lines},
              open(os.path.join(out_dir, f'n_{rid}.blocks.json'), 'w', encoding='utf-8'))
    if not quiet:
        print(f'{root}: {len(funcs[root]["regs"])} registers, closure {len(members)} functions '
              f'({sum(funcs[m]["lines"] for m in members)} lines), calls {stats["calls_closed"]} to copies + '
              f'{stats["calls_open"]} real, {blocks} blocks -> {shown(path)}{"" if not same else " (unchanged)"}')
    if not quiet and hand_members:
        print(f'    hand-written natives in front: {", ".join(hand_members)}; skip {", ".join(sorted(skip, key=order)) or "-"}')
    return {'root': root, 'id': rid, 'members': members, 'left_out': left_out, 'stats': stats}


def key_of(root, opts):
    if opts.get('key'):
        return opts['key']
    return 'banjo_cpu_native_' + (root if root.startswith('sub_') else re.sub(r'^rex_', '', root).lower())[:56]


def write_list(entries, out_dir, bulk=()):
    """banjo_hot_all.h (the copies) and banjo_hot_list.inc (one BANJO_HOT_HOOK line each) for banjo_hot_guest.cpp;
    banjo_hot_bulk_<k>.inc (the bulk list's headers and BANJO_HOT_HOOK_BULK lines) for banjo_hot_shard_<k>.cpp."""
    inc = ['// GENERATED by tools/localize_native.py from app/src/cpu/hot/localized.txt (do not edit).']
    allh = ['// GENERATED by tools/localize_native.py from app/src/cpu/hot/localized.txt (do not edit).', '#pragma once']
    for slot, (root, opts, what) in enumerate(entries):
        rid = identifier(root)
        allh.append(f'#include "n_{rid}.h"')
        desc = ('Native localized copy of %s: %s' % (root, what)).replace('\\', '/').replace('"', "'")
        inc.append(f'BANJO_HOT_HOOK({root}, {rid}, {key_of(root, opts)}, {"false" if opts.get("off") else "true"}, '
                   f'{slot}, "{desc}")')
    # The bulk list over the shards: the largest headers first, each to the shard with the fewest bytes so far.
    shards = [[0, []] for _ in range(SHARDS)]
    for root, opts, what in sorted(bulk, key=lambda e: -os.path.getsize(os.path.join(out_dir, f'n_{identifier(e[0])}.h'))):
        rid = identifier(root)
        shard = min(shards, key=lambda s: s[0])
        shard[0] += os.path.getsize(os.path.join(out_dir, f'n_{rid}.h'))
        desc = ('Bulk localized copy of %s: %s' % (root, what)).replace('\\', '/').replace('"', "'")
        shard[1] += [f'#include "n_{rid}.h"',
                     f'BANJO_HOT_HOOK_BULK({root}, {rid}, {key_of(root, opts)}, {"false" if opts.get("off") else "true"}, '
                     f'{BULK_SLOT}, "{desc}")']
    for k, (size, lines) in enumerate(shards):
        text = '\n'.join([f'// GENERATED by tools/localize_native.py --bulk from app/src/cpu/hot/localized_bulk.txt (do not '
                          f'edit): shard {k} of {SHARDS},', f'// {size // 1024} KB of copies, compiled by '
                          f'app/src/cpu/banjo_hot_shard_{k}.cpp.'] + lines) + '\n'
        path = os.path.join(out_dir, f'banjo_hot_bulk_{k}.inc')
        try:
            same = open(path, encoding='utf-8').read() == text
        except OSError:
            same = False
        if not same:
            os.makedirs(out_dir, exist_ok=True)  # nights-nx: an empty list made no copy, so no folder yet
            open(path, 'w', encoding='utf-8', newline='\n').write(text)
    # Headers of functions no longer in the lists (this tool's own output) go.
    keep = {f'n_{identifier(root)}' for root, _, _ in list(entries) + list(bulk)}
    if os.path.isdir(out_dir):
        for fn in os.listdir(out_dir):
            stem = fn[:-len('.blocks.json')] if fn.endswith('.blocks.json') else fn[:-2] if fn.endswith('.h') else None
            if stem and stem.startswith('n_') and stem not in keep:
                os.remove(os.path.join(out_dir, fn))
    for name, lines in (('banjo_hot_all.h', allh), ('banjo_hot_list.inc', inc)):
        path = os.path.join(out_dir, name)
        text = '\n'.join(lines) + '\n'
        try:
            same = open(path, encoding='utf-8').read() == text
        except OSError:
            same = False
        if not same:
            os.makedirs(out_dir, exist_ok=True)
            open(path, 'w', encoding='utf-8', newline='\n').write(text)


def survey(index, hooks, reach, names):
    for name in names:
        name = name if not re.fullmatch(r'[0-9A-Fa-f]{8}', name) else 'sub_' + name.upper()
        f = index.funcs.get(name)
        if f is None:
            print(f'{name}: not a generated function')
            continue
        why = fits(index, hooks, name)
        members, left_out = closure_of(index, hooks, name, 6000, 1200)
        opens = sorted(left_out.items())
        print(f'{name}: {f["lines"]} lines, {len(f["regs"])} ctx registers ({len(f["written"])} written), '
              f'{len(f["callees"])} direct callees, {f["unknown"]} indirect calls or traps'
              f'{"; CANNOT be a copy: " + why if why else ""}'
              f'{"; replaced by an app hook already?" if hooks.replaced(name) else ""}')
        print(f'    closure: {len(members)} functions, {sum(index.funcs[m]["lines"] for m in members)} lines'
              + (': ' + ', '.join(members[1:]) if len(members) > 1 else ''))
        for k, v in opens:
            print(f'    real call {k}: {v}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*')
    ap.add_argument('--gen', default=GEN)
    ap.add_argument('--out', default=OUT, help='folder for the headers (default app/generated/hot)')
    ap.add_argument('--list', nargs='?', const=LIST, help='localize every function of the list file')
    ap.add_argument('--bulk', nargs='?', const=BULK, help='with --list: the bulk list too (its hooks in shard files)')
    ap.add_argument('--survey', action='store_true', help='only say what a copy of each name would contain')
    ap.add_argument('--what', default='')
    a = ap.parse_args()
    entries = list_entries(a.list or LIST)
    index = Index(a.gen)
    hand = hand_entries()
    listed = {e[0] for e in entries}
    bulk = [e for e in list_entries(a.bulk) if e[0] not in listed and e[0] not in hand] if a.bulk else []
    # A bulk function is a native too: its copy touches no more than its original (and it is not an app hook).
    hooks = Hooks([e[0] for e in entries] + [e[0] for e in bulk])
    reach = Reach(index, hooks)
    for name in hand:
        if name not in index.funcs:
            sys.exit(f'localize_native: hand.txt: {name} is not a generated function')
    if a.survey:
        survey(index, hooks, reach, a.names)
        return
    if a.list:
        for root, opts, what in entries:
            localize(index, hooks, reach, root, what, opts, a.out, hand=hand)
        made, skipped = [], []
        for root, opts, what in bulk:
            try:
                localize(index, hooks, reach, root, what, opts, a.out, quiet=True, hand=hand)
                made.append((root, opts, what))
            except SystemExit as e:  # a form the tool refuses: that function stays generated code
                skipped.append(f'{root} ({str(e).replace("localize_native: ", "")[:90]})')
        write_list(entries, a.out, made)
        print(f'localize_native: {len(entries)} copies in {shown(a.out)}'
              + (f'; bulk: {len(made)} copies in {SHARDS} shards' if a.bulk else ''))
        if skipped:
            print(f'localize_native: bulk: {len(skipped)} left out:\n    ' + '\n    '.join(skipped))
        return
    if not a.names:
        ap.error('names, --list or --survey')
    for name in a.names:
        name = name if not re.fullmatch(r'[0-9A-Fa-f]{8}', name) else 'sub_' + name.upper()
        hooks.natives.add(name)
        localize(index, hooks, reach, name, a.what, {}, a.out, hand=hand)


if __name__ == '__main__':
    main()
