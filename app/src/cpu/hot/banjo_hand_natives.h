// banjo-nx - hand-written native versions of hot guest functions (CPU enhancements banjo_hand_*).
//
// The localized copies of tools/localize_native.py keep a function's own statements; these replace the statements:
// the XDK C runtime's memcpy and memset and Direct3D's two shader float-constant writers, written from what the
// routines do. They are sa2-nx's natives (app/src/cpu/hot/n_827F05C0.h, n_827EFEA0.h, n_d3d_constants.h, GPL-3.0;
// the memcpy came there from masseffect-nx's n_82AC4AF0.h): tools/crt_signatures.py of sa2-nx finds the same three C
// runtime routines in Banjo's image (unique matches), and the two constant writers are the same instructions with
// the same two offsets. Banjo's own additions are the two writers of Direct3D's state flush (namespace flush below).
// What is different here is how they are used and checked:
//
//   * Each is a template on the store policy of banjo_hot_common.h, so the guard's logged form records what it is
//     about to write like any generated copy (no hand-written list of written ranges).
//   * tools/localize_native.py puts them IN FRONT of the generated copy of the same function wherever that copy is
//     used: as a hook of its own, and inside every other function's copy that contains it (app/src/cpu/hot/hand.txt).
//     Inside a caller's copy the call is inlined: no hook, no hand-over of every register (on the Switch that
//     hand-over is what made copies with real calls slower than their originals).
//   * A native returns false to DECLINE a call it does not cover (nothing written yet): the generated copy of the
//     original then runs, in the same place.
//   * The registers the original leaves behind as scratch (hand.txt gives them per function; tools/localize_native.py
//     takes them from the original's own statements) are NOT reproduced: the natives write no register. Those
//     registers are left out of the guard's register compare for every hook whose copy contains the native. Sound
//     because they are caller-saved scratch of assembly routines and of a callee the callers reach through the
//     normal calling convention: a caller that read one after the call would get a different value, would act on
//     it, and the guard would see that in everything else it compares.
//
// ARM (the A57 is where this has to pay, and sa2-nx's first native Direct3D layer lost there for reasons no PC run
// showed: docs/api-renderer.md of sa2-nx, section 6): nothing here is per thread (no thread_local: a function call on
// the Switch), nothing copies a context, no loop keeps a count where a store could alias it, nothing is byte-swapped
// word by word (the copies are guest order to guest order), and sizes up to 128 bytes (memcpy; 64 for memset) are
// moved with overlapping 16/8/4/2-byte loads and stores without a call into the C library. In a PC run of title,
// menus, intro and gameplay 62 % of the 21.9 million memcpy calls were under 64 bytes and 21 % were 64 to 127; 46 %
// of the memsets were 4 to 7 bytes; 98 % of the 26 million constant uploads were 1 to 7 vectors.
//
// Guest function addresses may be named in this folder only (tools/direct_calls.py and tools/localize_native.py
// read every other source's addresses as hooked functions; these four are hooked, by app/src/cpu/hot/localized.txt).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/ppc/context.h>
#include <rex/ppc/diet_ops.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "cpu/banjo_hot_common.h"

namespace banjo::hand {

// The natives' own keys (banjo_hot_guest.cpp), each read once: 0 not read yet, 1 off, 2 on.
enum : uint32_t {
  kMemcpy = 0, kMemset = 1, kConstants = 2, kFlush = 3, kAudioFilter = 4, kXAudioMix = 5, kTransforms = 6, kBufferReset = 7,
  kNatives = 8
};
extern uint8_t g_state[kNatives];
uint8_t Init(uint32_t which);
[[gnu::always_inline]] inline bool On(uint32_t which) {
  const uint8_t state = g_state[which];
  if (state == 2) [[likely]] return true;
  return state == 0 && Init(which) == 2;
}

namespace detail {

// 16 bytes through a register (any alignment; one NEON load or store on the A57).
struct B16 {
  uint8_t b[16];
};
[[gnu::always_inline]] inline B16 L16(const uint8_t* p) {
  B16 v;
  std::memcpy(v.b, p, 16);
  return v;
}
[[gnu::always_inline]] inline void S16(uint8_t* p, const B16& v) { std::memcpy(p, v.b, 16); }

// 1 <= n <= 64; all loads before all stores (memmove semantics for dst <= src overlap).
[[gnu::always_inline]] inline void CopySmall(uint8_t* d, const uint8_t* s, uint32_t n) {
  if (n >= 16) {
    if (n > 32) {
      const B16 a = L16(s), b = L16(s + 16), c = L16(s + n - 32), e = L16(s + n - 16);
      S16(d, a);
      S16(d + 16, b);
      S16(d + n - 32, c);
      S16(d + n - 16, e);
    } else {
      const B16 a = L16(s), e = L16(s + n - 16);
      S16(d, a);
      S16(d + n - 16, e);
    }
  } else if (n >= 8) {
    uint64_t a, e;
    std::memcpy(&a, s, 8);
    std::memcpy(&e, s + n - 8, 8);
    std::memcpy(d, &a, 8);
    std::memcpy(d + n - 8, &e, 8);
  } else if (n >= 4) {
    uint32_t a, e;
    std::memcpy(&a, s, 4);
    std::memcpy(&e, s + n - 4, 4);
    std::memcpy(d, &a, 4);
    std::memcpy(d + n - 4, &e, 4);
  } else if (n >= 2) {
    uint16_t a, e;
    std::memcpy(&a, s, 2);
    std::memcpy(&e, s + n - 2, 2);
    std::memcpy(d, &a, 2);
    std::memcpy(d + n - 2, &e, 2);
  } else {
    *d = *s;
  }
}

// 64 < n <= 128; all loads before all stores. (Banjo's calls: a fifth are 64 to 127 bytes.)
[[gnu::always_inline]] inline void CopyMedium(uint8_t* d, const uint8_t* s, uint32_t n) {
  const B16 a = L16(s), b = L16(s + 16), c = L16(s + 32), e = L16(s + 48);
  const B16 f = L16(s + n - 64), g = L16(s + n - 48), h = L16(s + n - 32), i = L16(s + n - 16);
  S16(d, a);
  S16(d + 16, b);
  S16(d + 32, c);
  S16(d + 48, e);
  S16(d + n - 64, f);
  S16(d + n - 48, g);
  S16(d + n - 32, h);
  S16(d + n - 16, i);
}

// 1 <= n <= 64, every byte = v.
[[gnu::always_inline]] inline void FillSmall(uint8_t* d, uint8_t v, uint32_t n) {
  if (n >= 16) {
    B16 x;
    std::memset(x.b, v, 16);
    S16(d, x);
    S16(d + n - 16, x);
    if (n > 32) {
      S16(d + 16, x);
      S16(d + n - 32, x);
    }
  } else {
    const uint64_t x = uint64_t(v) * 0x0101010101010101ull;
    if (n >= 8) {
      std::memcpy(d, &x, 8);
      std::memcpy(d + n - 8, &x, 8);
    } else if (n >= 4) {
      std::memcpy(d, &x, 4);
      std::memcpy(d + n - 4, &x, 4);
    } else if (n >= 2) {
      std::memcpy(d, &x, 2);
      std::memcpy(d + n - 2, &x, 2);
    } else {
      *d = v;
    }
  }
}

// [a, a + n) is one stretch of host memory: it does not wrap around 4 GB and (on hosts with the physical-view
// offset) does not cross its boundary. n >= 1.
[[gnu::always_inline]] inline bool Straight(uint32_t a, uint32_t n) {
  if (uint64_t(a) + n > 0x100000000ull) return false;
  return ::banjo::hot::PhysOff(a) == ::banjo::hot::PhysOff(a + n - 1u);
}

}  // namespace detail

// sub_824FBC50 - XDK C runtime memcpy: r3 = dst, r4 = src, r5 = n (its 32-bit part); returns r3 as it came.
//
// The original stores r3 at -8(r1) first (a real store: it reloads r3 from there at every return), then copies
// FORWARD: a byte head up to 8-byte dst alignment, then 128-byte blocks / doublewords / words / bytes chosen by the
// alignment of src; dcbt / dcbtst are hints. Facts used:
//   * every size and address is the 32-bit part (cmplw, rlwinm, ea = u32 + u32);
//   * no loop reads past src + n or writes past dst + n;
//   * within each unit all loads precede its stores and units run upward: for dst <= src the result is memmove's,
//     for ranges that do not overlap a plain copy. For dst > src with dst - src < n the forward copy smears bytes it
//     already copied: DECLINED;
//   * n == 0 copies nothing (the save of r3 still happens).
// Also declined: ranges that wrap around 4 GB, and a destination that covers the save slot (the original would then
// return what was copied over it).
// Scratch the original leaves and this does not reproduce (hand.txt): r0, r4-r12.
namespace n_824FBC50 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kMemcpy)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32, slot = ctx.r1.u32 - 8u;
  s.Hand(kMemcpy, n);
  if (n) {
    if (uint32_t(dst - src - 1u) < n - 1u) [[unlikely]] return false;  // dst > src, dst - src < n
    if (!detail::Straight(dst, n) || !detail::Straight(src, n)) [[unlikely]] return false;
    if (uint64_t(uint32_t(slot + 7u - dst)) < uint64_t(n) + 7u) [[unlikely]] return false;  // dst covers the slot
  }
  if (!s.Note(slot, 8)) [[unlikely]] return true;
  const uint64_t saved = __builtin_bswap64(ctx.r3.u64);
  std::memcpy(::banjo::hot::Raw(base, slot), &saved, 8);
  if (n == 0) return true;
  if (!s.Note(dst, n)) [[unlikely]] return true;
  ::banjo::hot::Card(base, dst, n);
  uint8_t* d = ::banjo::hot::Raw(base, dst);
  const uint8_t* p = ::banjo::hot::Raw(base, src);
  if (n <= 64) {
    detail::CopySmall(d, p, n);
  } else if (n <= 128) {
    detail::CopyMedium(d, p, n);
  } else if (uint32_t(src - dst) < n) {
    std::memmove(d, p, n);  // dst < src (or equal), overlapping: the forward copy is memmove
  } else {
    std::memcpy(d, p, n);
  }
  return true;
}

}  // namespace n_824FBC50

// sub_824FBB50 - XDK C runtime memset: r3 = dst, r4 = value (its low byte), r5 = n; r3 is not written.
//
// The original (ori r6,r3,0; ctr = n + 1): single bytes until r6 is 4-aligned or n is used up, the low byte of r4
// replicated into a word, 16-byte blocks of four stw, up to three stw, up to three stb. Every store writes that byte,
// each byte of [dst, dst + n) exactly once, nothing outside, and nothing is read. Facts used:
//   * ctr = r5 + 1 is 64-bit: with anything in the upper half of r5 the head loop could run past n: DECLINED;
//   * ranges that wrap around 4 GB are declined.
// Scratch not reproduced (hand.txt): r0, r4, r5, r6.
namespace n_824FBB50 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kMemset)) [[unlikely]] return false;
  if (ctx.r5.u64 >> 32) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, n = ctx.r5.u32;
  s.Hand(kMemset, n);
  if (n == 0) return true;
  if (!detail::Straight(dst, n)) [[unlikely]] return false;
  if (!s.Note(dst, n)) [[unlikely]] return true;
  ::banjo::hot::Card(base, dst, n);
  uint8_t* d = ::banjo::hot::Raw(base, dst);
  if (n <= 64) {
    detail::FillSmall(d, ctx.r4.u8, n);
  } else {
    std::memset(d, int(ctx.r4.u8), n);
  }
  return true;
}

}  // namespace n_824FBB50

// nights-nx (2026-10-29): NiGHTS's two matrix-stack transforms, called for every point and direction the game
// projects on the main thread (emulator ARM profile, gameplay: 4.0 % and 1.9 % of its samples, ranks 3 and 6):
//   sub_822D9CB0: r3 = object, r4 = float3 in, r5 = float3 out. The matrix stack is the word at r3 + 4 (64-byte
//                 matrices, the top's index at stack + 4096; an index of 63 or more means guest address 0). The point
//                 (x, y, z, 1) times the top matrix, rows summed as x m0 + (y m1 + (z m2 + m3)), then divided by its
//                 w: vrefp (here, as in the generated code, 1 / w exactly) and two Newton-Raphson steps
//                 r' = r e + r with e = -(w r - 1), the first estimate kept when the refined one is a NaN (vsel).
//   sub_822D9DD8: the same object and arguments; the direction x m0 + (y m1 + z m2) (no translation, no divide).
// Both load the float3 with lvlx/lvrx (its fourth element is never used), the rows with lvlx/lvrx pairs inside
// [matrix, matrix + 64) (48 for the direction), store the three results with stvewx at r1 - 16, -12, -8, load them
// back as words and store those at r5 + 8, + 4, + 0. Facts used:
//   * the generated code computes lane by lane with separate multiplies and adds (-ffp-contract=off on the whole
//     target) after fpscr.enableFlushMode(); the same scalar operations in the same order and operand order give the
//     same bits (the host flushes denormals for scalar and vector single precision alike: FPCR.FZ, MXCSR FTZ/DAZ);
//     the negation is a sign flip in both (vxor with the sign bit);
//   * all loads precede all stores; the result reaches the stack slot and the destination, so a destination that
//     overlaps the slot is DECLINED, as are the early returns (r4, r5 or the stack 0), an index of 63 or more, a
//     stack pointer that is not word-aligned and addresses from 0xE0000000 (the physical views);
//   * the integer registers the original leaves in ctx are set as it leaves them (hand.txt exact=); its vector
//     registers (v0, v7-v13, v45-v63: volatile in the Xbox 360 calling convention) are scratch this does not reproduce.
//     r11 is not written: with non_argument_as_local the generated original keeps it in a C++ local, not in ctx.
namespace transforms {

[[gnu::always_inline]] inline uint32_t Word(const uint8_t* p) {
  uint32_t w;
  std::memcpy(&w, p, 4);
  return __builtin_bswap32(w);
}
[[gnu::always_inline]] inline float Float(const uint8_t* p) {
  const uint32_t w = Word(p);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}
[[gnu::always_inline]] inline uint32_t Bits(float f) {
  uint32_t w;
  std::memcpy(&w, &f, 4);
  return w;
}
[[gnu::always_inline]] inline void PutWord(uint8_t* p, uint32_t w) {
  w = __builtin_bswap32(w);
  std::memcpy(p, &w, 4);
}
[[gnu::always_inline]] inline bool Low(uint32_t a, uint32_t n) { return uint64_t(a) + n <= 0xE0000000ull; }

// The top matrix of the stack the object names, or 0 for a call this does not cover.
[[gnu::always_inline]] inline uint32_t Top(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t src = ctx.r4.u32, dst = ctx.r5.u32, slot = ctx.r1.u32 - 16u;
  if (!src || !dst || (slot & 3u) || !Low(ctx.r3.u32 + 4u, 4)) [[unlikely]] return 0;
  const uint32_t stack = Word(base + uint32_t(ctx.r3.u32 + 4u));
  if (!stack || !Low(stack + 4096u, 4)) [[unlikely]] return 0;
  const int32_t index = int32_t(Word(base + uint32_t(stack + 4096u)));
  if (index >= 63 || index < 0) [[unlikely]] return 0;  // 63 or more: guest address 0; below 0: never seen
  const uint32_t matrix = stack + (uint32_t(index) << 6);
  if (!matrix || !Low(matrix, 64) || !Low(src, 12) || !Low(dst, 12) || !Low(slot, 12)) [[unlikely]] return 0;
  if (uint32_t(dst - slot + 11u) < 23u) [[unlikely]] return 0;  // [dst, dst + 12) meets [slot, slot + 12)
  return matrix;
}

// The three results to the stack slot and the destination, as the original's stvewx / lwz / stw leave them.
template <class S>
[[gnu::always_inline]] inline void Store(PPCContext& __restrict ctx, uint8_t* base, S& s, uint32_t x, uint32_t y,
                                         uint32_t z) {
  const uint32_t slot = ctx.r1.u32 - 16u, dst = ctx.r5.u32;
  if (!s.Note(slot, 12)) [[unlikely]] return;
  ::banjo::hot::Card(base, slot, 12);
  PutWord(base + slot, x);
  PutWord(base + slot + 4u, y);
  PutWord(base + slot + 8u, z);
  if (!s.Note(dst, 12)) [[unlikely]] return;
  ::banjo::hot::Card(base, dst, 12);
  PutWord(base + dst + 8u, z);
  PutWord(base + dst + 4u, y);
  PutWord(base + dst, x);
}

}  // namespace transforms

namespace n_822D9CB0 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kTransforms)) [[unlikely]] return false;
  const uint32_t matrix = transforms::Top(ctx, base);
  if (!matrix) [[unlikely]] return false;
  s.Hand(kTransforms, 0);
  const uint8_t* p = base + ctx.r4.u32;
  const uint8_t* m = base + matrix;
  ctx.fpscr.enableFlushMode();
  const float x = transforms::Float(p), y = transforms::Float(p + 4), z = transforms::Float(p + 8);
  float v[4];
  for (int c = 0; c < 4; ++c) {
    float a = transforms::Float(m + 32 + 4 * c) * z + transforms::Float(m + 48 + 4 * c);
    a = y * transforms::Float(m + 16 + 4 * c) + a;
    v[c] = x * transforms::Float(m + 4 * c) + a;
  }
  const float w = v[3];
  const float r0 = 1.0f / w;
  const float e0 = -(w * r0 - 1.0f);
  const float r1 = r0 * e0 + r0;
  const float e1 = -(w * r1 - 1.0f);
  const float r2 = r1 * e1 + r1;
  const float r = r1 == r1 ? r2 : r0;
  const uint32_t rx = transforms::Bits(v[0] * r), ry = transforms::Bits(v[1] * r), rz = transforms::Bits(v[2] * r);
  transforms::Store(ctx, base, s, rx, ry, rz);
  ctx.r3.u64 = ctx.r1.u64 - 16;
  ctx.r4.u64 = ctx.r1.u64 - 16;
  ctx.r10.u64 = ctx.r1.u64 - 16;
  ctx.r6.u64 = ry;
  ctx.r7.u64 = rx;
  ctx.r8.u64 = rz;
  ctx.r9.u64 = 8;
  return true;
}

}  // namespace n_822D9CB0

namespace n_822D9DD8 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kTransforms)) [[unlikely]] return false;
  const uint32_t matrix = transforms::Top(ctx, base);
  if (!matrix) [[unlikely]] return false;
  s.Hand(kTransforms, 1);
  const uint8_t* p = base + ctx.r4.u32;
  const uint8_t* m = base + matrix;
  ctx.fpscr.enableFlushMode();
  const float x = transforms::Float(p), y = transforms::Float(p + 4), z = transforms::Float(p + 8);
  uint32_t v[3];
  for (int c = 0; c < 3; ++c) {
    float a = z * transforms::Float(m + 32 + 4 * c);
    a = y * transforms::Float(m + 16 + 4 * c) + a;
    v[c] = transforms::Bits(x * transforms::Float(m + 4 * c) + a);
  }
  transforms::Store(ctx, base, s, v[0], v[1], v[2]);
  ctx.r3.u64 = 4;
  ctx.r4.u64 = v[1];
  ctx.r6.u64 = v[0];
  ctx.r7.u64 = v[2];
  ctx.r8.u64 = 8;
  ctx.r9.u64 = ctx.r1.u64 - 16;
  ctx.r10.u64 = 16;
  return true;
}

}  // namespace n_822D9DD8

// sub_823EAFD8 - nights-nx (2026-10-29): resets up to 16 float buffers of a block (the second game thread's hottest
// function in the emulator ARM profile: 26 % of its samples). r3 = block: count at +64, the buffers' length in floats
// at +72, buffer pointers at +0 + 4 i, a flag byte per buffer at +80 + i. For i < min(count, 16): length at +72
// stored back as it was, buffer[0] = the float at 0x82035340, buffer[1 .. length) = the float at 0x820007CC (lfs /
// stfs through the double registers f13 / f0), flag i = 0 (r7). Facts used:
//   * the length and the count are loaded again after every store: a buffer that meets [block, block + 96) (pointers,
//     count, length, flags) is DECLINED, so both stay what they were and the loop is the plain fill here;
//   * a buffer is filled before the next one's pointer is read: buffers that overlap each other end as here (the
//     same order);
//   * the floats are converted as the generated code converts them (rex::ppc::SingleToDoubleAnyMode and back);
//   * a count of 0 (the early return) is declined; every register the original writes in ctx is set as it leaves it
//     (r11 and f0 are C++ locals of the generated code under non_argument_as_local: not written here either).
namespace n_823EAFD8 {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kBufferReset)) [[unlikely]] return false;
  const uint32_t block = ctx.r3.u32;
  if (!transforms::Low(block, 96) || (block & 3u)) [[unlikely]] return false;
  const uint8_t* b = base + block;
  const uint32_t count = transforms::Word(b + 64), length = transforms::Word(b + 72);
  if (count == 0) [[unlikely]] return false;
  const uint32_t buffers = count < 16 ? count : 16;
  const uint32_t stores = length > 1 ? length : 1;
  if (stores > 0x04000000u) [[unlikely]] return false;
  for (uint32_t i = 0; i < buffers; ++i) {
    const uint32_t p = transforms::Word(b + 4 * i);
    if (!transforms::Low(p, 4 * stores)) [[unlikely]] return false;
    if (uint64_t(p) + 4 * stores > block && p < block + 96u) [[unlikely]] return false;
  }
  s.Hand(kBufferReset, length);
  const double f13 = rex::ppc::SingleToDoubleAnyMode(transforms::Word(base + 0x82035340u));
  const double f0 = rex::ppc::SingleToDoubleAnyMode(transforms::Word(base + 0x820007CCu));
  const uint32_t first = __builtin_bswap32(transforms::Bits(rex::ppc::DoubleToSingleAnyMode(f13)));
  const uint32_t rest = __builtin_bswap32(transforms::Bits(rex::ppc::DoubleToSingleAnyMode(f0)));
  uint32_t last = 0;
  for (uint32_t i = 0; i < buffers; ++i) {
    if (!s.Note(block + 72u, 4)) [[unlikely]] return true;
    ::banjo::hot::Card(base, block + 72u, 4);  // the length stored back unchanged
    const uint32_t p = transforms::Word(b + 4 * i);
    if (!s.Note(p, 4 * stores)) [[unlikely]] return true;
    ::banjo::hot::Card(base, p, 4 * stores);
    uint8_t* d = base + p;
    std::memcpy(d, &first, 4);
    uint32_t k = 1;
#if defined(__aarch64__)
    const uint32x4_t r4 = vdupq_n_u32(rest);
    for (; k + 4 <= stores; k += 4) vst1q_u8(d + 4 * k, vreinterpretq_u8_u32(r4));
#endif
    for (; k < stores; ++k) std::memcpy(d + 4 * k, &rest, 4);
    if (!s.Note(block + 80u + i, 1)) [[unlikely]] return true;
    ::banjo::hot::Card(base, block + 80u + i, 1);
    base[block + 80u + i] = 0;
    last = p + 4 * (stores - 1);
  }
  ctx.f13.f64 = f13;
  ctx.r6.u64 = length;
  ctx.r7.u64 = 0;
  ctx.r8.u64 = ctx.r3.u64 + 4ull * count;
  ctx.r9.u64 = count;
  ctx.r10.u64 = last;
  return true;
}

}  // namespace n_823EAFD8

// Direct3D's two shader float-constant writers (called for every constant upload of every draw):
//   sub_8222EFA8: r3 = device, r4 = first constant, r5 = source (any alignment), r6 = count of 16-byte vectors,
//                 r7 = 64-bit dirty bits. Destination ((r3 + (r4 + 120) * 16) & ~15) + 16 k; dirty mask at r3 + 0.
//   sub_8222F0D8: the same with (r4 + 376) and the dirty mask at r3 + 8.
// The originals copy four vectors per iteration, then one per iteration: lvlx + lvrx + vor is an unaligned 16-byte
// load (lvrx at an aligned address loads nothing), stvx128 an aligned store, dcbt are hints; then ld / or r7 / std on
// the dirty mask. Facts used:
//   * every address is the 32-bit part. The counters here are 32-bit compares (addic. / cmplwi), but r9 = r6 is
//     decremented as 64 bits: a count with anything in its upper half is DECLINED;
//   * the four-at-a-time loop loads its four vectors before it stores them: ranges that overlap are DECLINED, as are
//     ranges that wrap around 4 GB;
//   * the dirty mask is read after the copy, in the same order here;
//   * the originals' first instruction saves r31 at -8(r1). The generated code keeps r31 in a local of the function
//     (zero at that point, the context's r31 is never touched) and makes the store unless the build elides dead
//     saves (REX_NV_SAVES_ELIDE, off in the shipped build): eight zero bytes there, written first, as the original
//     does. (Should a later code generator keep r31 in the context instead, the guard reports the slot's bytes.)
// Scratch not reproduced (hand.txt): r4, r5, r6, r8-r11, v0, v10-v13.
namespace constants {

template <class S>
[[gnu::always_inline]] inline bool Upload(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s,
                                          uint32_t first, uint32_t mask_offset) {
  if (!On(kConstants)) [[unlikely]] return false;
  if (ctx.r6.u64 >> 32) [[unlikely]] return false;
  const uint32_t count = ctx.r6.u32, src = ctx.r5.u32, mask = ctx.r3.u32 + mask_offset;
  const uint32_t dst = (((ctx.r4.u32 + first) << 4) + ctx.r3.u32) & ~15u;
  s.Hand(kConstants, count);
  if (mask > 0xFFFFFFF7u) [[unlikely]] return false;
  if (count > 0x0FFFFFFFu) [[unlikely]] return false;
  const uint32_t bytes = count << 4;
  if (count) {
    if (!detail::Straight(dst, bytes) || !detail::Straight(src, bytes)) [[unlikely]] return false;
    if (uint32_t(src - dst) < bytes || uint32_t(dst - src) < bytes) [[unlikely]] return false;
  }
#if !defined(REX_NV_SAVES_ELIDE)
  {
    const uint32_t slot = ctx.r1.u32 - 8u;  // std r31,-8(r1), the local r31 (zero)
    if (!s.Note(slot, 8)) [[unlikely]] return true;
    std::memset(::banjo::hot::Raw(base, slot), 0, 8);
  }
#endif
  if (count) {
    if (!s.Note(dst, bytes)) [[unlikely]] return true;
    ::banjo::hot::Card(base, dst, bytes);
    uint8_t* d = ::banjo::hot::Raw(base, dst);
    const uint8_t* p = ::banjo::hot::Raw(base, src);
    if (count <= 8) {
      const uint8_t* const end = p + bytes;  // (a pointer cursor: no store here can alias the loop's bound)
      do {
        detail::S16(d, detail::L16(p));
        d += 16;
        p += 16;
      } while (p != end);
    } else {
      std::memcpy(d, p, bytes);
    }
  }
  if (!s.Note(mask, 8)) [[unlikely]] return true;
  ::banjo::hot::Card(base, mask, 8);
  uint64_t m;
  std::memcpy(&m, ::banjo::hot::Raw(base, mask), 8);
  m |= __builtin_bswap64(ctx.r7.u64);
  std::memcpy(::banjo::hot::Raw(base, mask), &m, 8);
  return true;
}

}  // namespace constants

namespace n_8222EFA8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return constants::Upload(ctx, base, s, 120, 0);
}
}  // namespace n_8222EFA8

namespace n_8222F0D8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return constants::Upload(ctx, base, s, 376, 8);
}
}  // namespace n_8222F0D8

// Direct3D's state flush, the two functions that turn dirty bits into PM4 packets in the command buffer (called by
// the flush of every draw, once per dirty block of state):
//   sub_8222CB90: r3 = device, r4 = 64 dirty bits (bit 63 first), r5 = register number of the first bit, r6 = the
//                 shadow words (word i belongs to bit i). For every run of set bits: a type-0 header
//                 ((length - 1) << 16 | register) and the run's words, appended after the write pointer at device + 48
//                 (which names the last word written); the new pointer is stored at the end.
//   sub_8222D0E8: the same for the float constants, one bit per block of four vectors (16 registers, 64 bytes): the
//                 run is first padded with type-2 packets (0x80000000) so that its vectors land 16-aligned (the
//                 original stores them with stvx), then header ((16 * blocks - 1) << 16 | register) and the blocks.
// Facts used:
//   * both reload the buffer's limit (device + 52) for every run and call Direct3D's "more space" function when a
//     run does not fit: a call in which a run could fail is DECLINED before anything is written (Runs below);
//   * no dirty bit at all is declined (the originals would then write a run of length 0 and loop);
//   * words go from guest memory to guest memory unchanged: no byte order is touched;
//   * the constants' source must be 16-aligned and the write pointer a multiple of 4 (the vector loads and stores
//     round their addresses down): otherwise declined;
//   * the buffer between the write pointer and its limit must not overlap the shadow words or the device's two
//     pointer words (declined), and no address may be near the top of the 4 GB;
//   * the originals' prologue links a stack frame (stwu r1,-144 / -176): the old stack pointer is stored at the new
//     one unless the build elides the back chain (REX_BACK_CHAIN_ELIDE, off in the shipped build). Their saves of the
//     non-volatile registers are not stores in the generated code (those registers are locals of the function).
// Scratch not reproduced (hand.txt): r4-r12, and v0, v11-v13 for the constants. r1 and r3 end as they came.
namespace flush {

[[gnu::always_inline]] inline uint32_t Load32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, ::banjo::hot::Raw(base, a), 4);
  return __builtin_bswap32(v);
}
[[gnu::always_inline]] inline void Store32(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
// [a, a + n) and [b, b + m) share a byte (n, m >= 1; neither wraps).
[[gnu::always_inline]] inline bool Overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m) {
  return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
}

// kUnit: bytes per dirty bit (4: register words, 64: constant blocks). kFrame: the original's frame size.
//
// One pass, no register saved for it, and only the tests exactness needs. (The first version checked every run in a
// pass of its own and compared the exact ranges: 131 A57 instructions for a call with one short run, where the
// recompiled original takes 152 on average. A second, in one pass but with every pair of ranges tested, still took
// 119. Measured with tools/hot_arm_compare.py and the Switch build's disassembly; none of it showed on the PC.)
//   * the runs are written into the window between the write pointer and the buffer's limit, so that window as a
//     whole is what must not touch the 64 shadow units, the device's two pointer words or the frame word;
//   * a run that does not fit is where the original calls for more space. Whether any run could fail is decided
//     up front from the number of dirty bits alone: n bits make at most n runs, each at most 16 bytes of padding and
//     header besides its words, so with start + 16 n + (bytes of n units) + 12 below the limit every run passes the
//     original's own test and none of them is tested again. Otherwise the call is DECLINED before anything is
//     written (about 1 call in 1000 in a PC run; the original runs, with its call for space). (Handing the rest of
//     the bits to the original at the first run that fails is also exact, but those calls reach a call the guard
//     cannot repeat, so that path would have run unchecked.)
template <class S, uint32_t kUnit, uint32_t kFrame>
[[gnu::always_inline]] inline bool Runs(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kFlush)) [[unlikely]] return false;
  uint64_t m = ctx.r4.u64;
  if (m == 0) [[unlikely]] return false;
  const uint32_t device = ctx.r3.u32, reg0 = ctx.r5.u32, src = ctx.r6.u32, frame = ctx.r1.u32 - kFrame;
  constexpr uint32_t kShadow = 64u * kUnit;  // the shadow bytes 64 dirty bits can name (256 or 4096)
  // No address near the top of the 4 GB, so no sum below wraps.
  if (device > 0xFFFFE000u || frame > 0xFFFFE000u || src > 0xFFFFE000u) [[unlikely]] return false;
  if (kUnit == 64 && (src & 15u)) [[unlikely]] return false;
  // The original's first store, the frame's back chain, comes before it reads anything. Made here before the checks
  // that need the buffer's pointers: should one of them decline, the original starts by storing the same word.
#if !defined(REX_BACK_CHAIN_ELIDE)
  if (!s.Note(frame, 4)) [[unlikely]] return true;
  Store32(::banjo::hot::Raw(base, frame), ctx.r1.u32);  // stwu r1,-frame(r1)
#endif
  const uint32_t start = Load32(base, device + 48), limit = Load32(base, device + 52);
  constexpr uint32_t kWordsPerBit = kUnit / 4;
  const uint32_t bits = uint32_t(__builtin_popcountll(m));
  // Every run fits as the original judges it (above); this also says limit > start + 12.
  if (uint64_t(start) + 16u * bits + 4u * kWordsPerBit * bits + 12u >= limit) [[unlikely]] return false;
  if (limit > 0xFFFFFFE0u || (kUnit == 64 && (start & 3u))) [[unlikely]] return false;
  // Every word written lies in [start + 4, limit + 8): a run is only written when its last word is below limit.
  // That window must not touch the shadow units (they are read while it is written) nor the device's two pointer
  // words (the original reads the limit again for every run and stores the pointer at the end). The frame word
  // needs no test: it is written before everything else, here as there.
  const uint32_t window = limit - start + 8u;
  if (Overlap(start, window, src, kShadow) || Overlap(start, window, device + 48, 8)) [[unlikely]] return false;
  if (::banjo::hot::PhysOff(start) != ::banjo::hot::PhysOff(limit + 7u) ||
      ::banjo::hot::PhysOff(src) != ::banjo::hot::PhysOff(src + kShadow - 1u))
    [[unlikely]] return false;
  s.Hand(kFlush, bits);
  uint8_t* const buffer = ::banjo::hot::Raw(base, start);  // the word the write pointer names
  const uint8_t* const shadow = ::banjo::hot::Raw(base, src);
  uint32_t wp = start;
  uint32_t bit = 0;  // the dirty bit the top of m stands for
  do {
    const uint32_t zeros = uint32_t(__builtin_clzll(m));
    m <<= zeros;
    bit += zeros;
    const uint32_t length = ~m ? uint32_t(__builtin_clzll(~m)) : 64u;
    const uint32_t words = length * kWordsPerBit, bytes = words * 4u;
    // (sub_8222CB90's test, wp + 4 * length < limit, and sub_8222D0E8's, wp + 4 * (16 * blocks + 3) < limit, hold
    // for every run: decided above.)
    uint32_t pads = 0;
    if (kUnit == 64) {
      const uint32_t at = wp & 15u;  // 8: the header lands on 12 already
      pads = at == 4 ? 1u : at == 0 ? 2u : at == 12 ? 3u : 0u;
    }
    const uint32_t run_bytes = 4u * pads + 4u + bytes;
    if (!s.Note(wp + 4u, run_bytes)) [[unlikely]] return true;
    ::banjo::hot::Card(base, wp + 4u, run_bytes);
    uint8_t* d = buffer + (wp - start) + 4;
    for (uint32_t k = 0; k < pads; ++k) {
      Store32(d, 0x80000000u);
      d += 4;
    }
    Store32(d, (((words - 1u) << 16) & 0xFFFF0000u) | (reg0 + bit * kWordsPerBit));
    d += 4;
    const uint8_t* p = shadow + size_t(bit) * kUnit;
    if (bytes <= 64) {
      detail::CopySmall(d, p, bytes);
    } else {
      std::memcpy(d, p, bytes);
    }
    wp += run_bytes;
    bit += length;
    m = length == 64 ? 0 : m << length;
  } while (m);
  if (!s.Note(device + 48, 4)) [[unlikely]] return true;
  ::banjo::hot::Card(base, device + 48, 4);
  Store32(::banjo::hot::Raw(base, device + 48), wp);
  return true;
}

}  // namespace flush

namespace n_8222CB90 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return flush::Runs<S, 4, 144>(ctx, base, s);
}
}  // namespace n_8222CB90

namespace n_8222D0E8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return flush::Runs<S, 64, 176>(ctx, base, s);
}
}  // namespace n_8222D0E8

// XAudio's per-voice state-variable filter, sub_82B0DCB8 (banjo-nx 2026-10-23, the user's request: hand-optimise the
// audio thread; the hottest guest function of that thread, a fifth of it in the console's city profile).
//   r3 = the filter, r4 = the voice's frame buffer. It calls XAudioFrameBuffer::GetProcessingData (0x826662E0), which
//   copies the buffer's words +12, +16, +20 to its stack frame (+80): byte +81 is the channel count, +88 the samples
//   (256 floats a channel, a channel every 1024 bytes). With byte filter+24 set, the six channels' state (low at
//   +32, band at +56, a float each) is zeroed first. Then for each channel and sample, with q at +28, frequency at
//   +80 and the mode at +20 (> 0 high-pass, < 0 low-pass, 0 band-pass):
//     d = x - low; high = -(fma(q, band, -d)); band += freq * high; low += freq * band; x = the mode's output
//   each a float result of a double multiply-add (as the generated code computes fnmsubs / fmadds).
// The original stores band, low and the sample at every sample and reloads q, the frequency, the mode and the buffer
// pointer every time, each a byte-swapped guest access (nothing it writes can alias them: declined otherwise). Here
// the state stays in registers and the channels run side by side, a scalar chain each (ChannelsN). Bit-identical: the
// same double operations in the same order, rounded to float where the original rounds (-0.0 included: the negation
// follows the multiply-add as there); flush to zero off as the original sets it.
// Stores made as the original's (its frame: LR save, the save slot of r31, a local of the copy so 0, the back chain,
// GetProcessingData's three words), so the guard compares everything. Declined: no buffer object, more than 6
// channels, a buffer that overlaps the filter's words or the frame, a range that is not one stretch of memory.
// Scratch not reproduced (hand.txt): r4-r12, f0, f10-f13. r1 ends as it came, r3 = 0.
namespace audio_filter {

[[gnu::always_inline]] inline uint32_t Load32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, ::banjo::hot::Raw(base, a), 4);
  return __builtin_bswap32(v);
}
[[gnu::always_inline]] inline float LoadF(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
[[gnu::always_inline]] inline void StoreF(uint8_t* p, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
[[gnu::always_inline]] inline void Store32(uint8_t* p, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
[[gnu::always_inline]] inline bool Overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m) {
  return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
}

template <int kMode, uint32_t N>
[[gnu::always_inline]] inline void ChannelsN(uint8_t* samples, float q, float freq, float* low, float* band) {
  float l[N], b[N];
  for (uint32_t c = 0; c < N; ++c) {
    l[c] = low[c];
    b[c] = band[c];
  }
  // banjo-nx (2026-10-23, batch 6): high = -h with h = float(fma(q, band, -d)) (rounding is symmetric: float(-x) is
  // -float(x)), and freq * high = (-freq) * h exactly, so the band update takes h and -freq: the negation leaves the
  // chain each sample waits on (one instruction of its latency). The same values, -0.0 included; a NaN's sign is the
  // only bit that can differ, as with the reverb (real audio has none).
  const double dq = double(q), dfreq = double(freq), ndfreq = -dfreq;
  for (uint32_t i = 0; i < 256; ++i) {
    for (uint32_t c = 0; c < N; ++c) {
      uint8_t* at = samples + 1024u * c + 4u * i;
      const double d = double(LoadF(at) - l[c]);                        // fsubs
      const float h = float(std::fma(dq, double(b[c]), -d));            // fnmsubs before its negation
      b[c] = float(std::fma(ndfreq, double(h), double(b[c])));          // fmadds: freq * high + band
      l[c] = float(std::fma(dfreq, double(b[c]), double(l[c])));        // fmadds
      StoreF(at, kMode > 0 ? -h : kMode < 0 ? l[c] : b[c]);
    }
  }
  for (uint32_t c = 0; c < N; ++c) {
    low[c] = l[c];
    band[c] = b[c];
  }
}

template <int kMode>
[[gnu::always_inline]] inline void Channels(uint8_t* samples, uint32_t nch, float q, float freq, float* low,
                                            float* band) {
  switch (nch) {
  case 1: ChannelsN<kMode, 1>(samples, q, freq, low, band); break;
  case 2: ChannelsN<kMode, 2>(samples, q, freq, low, band); break;
  case 3: ChannelsN<kMode, 3>(samples, q, freq, low, band); break;
  case 4: ChannelsN<kMode, 4>(samples, q, freq, low, band); break;
  case 5: ChannelsN<kMode, 5>(samples, q, freq, low, band); break;
  default: ChannelsN<kMode, 6>(samples, q, freq, low, band); break;
  }
}

template <class S>
[[gnu::always_inline]] inline bool Run(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kAudioFilter)) [[unlikely]] return false;
  const uint32_t self = ctx.r3.u32, fb = ctx.r4.u32, sp = ctx.r1.u32, frame = sp - 112u;
  if (fb == 0 || self > 0xFFFFE000u || fb > 0xFFFFE000u || sp > 0xFFFFE000u || sp < 0x1000u) [[unlikely]]
    return false;
  // GetProcessingData's three words (+12, +16, +20 of the buffer object).
  const uint32_t w0 = Load32(base, fb + 12), w1 = Load32(base, fb + 16), samples = Load32(base, fb + 20);
  const uint32_t nch = (w0 >> 16) & 0xFFu;  // byte +81 of the frame: the word's second byte
  if (nch > 6) [[unlikely]] return false;
  if (::banjo::hot::PhysOff(self + 20) != ::banjo::hot::PhysOff(self + 83) ||
      ::banjo::hot::PhysOff(frame) != ::banjo::hot::PhysOff(sp - 1))
    [[unlikely]] return false;
  const uint32_t bytes = 1024u * nch;
  if (nch) {
    if (samples > 0xFFFFFFFFu - bytes || ::banjo::hot::PhysOff(samples) != ::banjo::hot::PhysOff(samples + bytes - 1) ||
        Overlap(samples, bytes, self + 20, 64) || Overlap(samples, bytes, frame, 112))
      [[unlikely]] return false;
  }
  s.Hand(kAudioFilter, nch);
  // The frame, as the original makes it (each store unless the build elides it).
#if !defined(REX_LR_MOVES_ELIDE)
  if (!s.Note(sp - 8, 4)) [[unlikely]] return true;
  Store32(::banjo::hot::Raw(base, sp - 8), uint32_t(ctx.lr));
#endif
#if !defined(REX_NV_SAVES_ELIDE)
  if (!s.Note(sp - 16, 8)) [[unlikely]] return true;
  std::memset(::banjo::hot::Raw(base, sp - 16), 0, 8);  // r31 is a local of the copy: its save writes 0
#endif
#if !defined(REX_BACK_CHAIN_ELIDE)
  if (!s.Note(frame, 4)) [[unlikely]] return true;
  Store32(::banjo::hot::Raw(base, frame), sp);
#endif
  if (!s.Note(frame + 80, 12)) [[unlikely]] return true;
  ::banjo::hot::Card(base, frame + 80, 12);
  uint8_t* const out = ::banjo::hot::Raw(base, frame + 80);
  Store32(out, w0);
  Store32(out + 4, w1);
  Store32(out + 8, samples);
  uint8_t* const f = ::banjo::hot::Raw(base, self);
  float low[6], band[6];
  if (f[24] != 0) {
    ctx.fpscr.disableFlushMode();
    if (!s.Note(self + 32, 48)) [[unlikely]] return true;
    ::banjo::hot::Card(base, self + 32, 48);
    std::memset(f + 32, 0, 48);  // the constant at 0x8213AE04: 0.0f
  }
  if (nch) {
    ctx.fpscr.disableFlushMode();
    for (uint32_t c = 0; c < nch; ++c) {
      low[c] = LoadF(f + 32 + 4 * c);
      band[c] = LoadF(f + 56 + 4 * c);
    }
    const float q = LoadF(f + 28), freq = LoadF(f + 80);
    const int32_t mode = int32_t(Load32(base, self + 20));
    if (!s.Note(samples, bytes) || !s.Note(self + 32, 4 * nch) || !s.Note(self + 56, 4 * nch)) [[unlikely]]
      return true;
    ::banjo::hot::Card(base, samples, bytes);
    ::banjo::hot::Card(base, self + 32, 4 * nch);
    ::banjo::hot::Card(base, self + 56, 4 * nch);
    uint8_t* const p = ::banjo::hot::Raw(base, samples);
    if (mode > 0)
      Channels<1>(p, nch, q, freq, low, band);
    else if (mode < 0)
      Channels<-1>(p, nch, q, freq, low, band);
    else
      Channels<0>(p, nch, q, freq, low, band);
    for (uint32_t c = 0; c < nch; ++c) {
      StoreF(f + 32 + 4 * c, low[c]);
      StoreF(f + 56 + 4 * c, band[c]);
    }
  }
  ctx.r3.u64 = 0;
  return true;
}

}  // namespace audio_filter

namespace n_82B0DCB8 {
template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, S& s) {
  return audio_filter::Run(ctx, base, s);
}
}  // namespace n_82B0DCB8

// XAudio's ramped channel mix, rex_XAudioMixFrameBufferChannel (banjo-nx 2026-10-23, batch 6, the user's request: the
// audio mixer further): the loop of XAUDIO::CRouterEffect::Process, a sixth of the audio thread in the console's city
// profile. r3 = the destination channel, r4 = the source channel (256 big-endian floats each), r5 = the gain at the
// first sample (a vector, address rounded down to 16), r6 = the gain step per sample (likewise). The original (hand
// written VMX): saves r31 at -8(r1) (a local of the copy, so 0: the save writes 0 unless the build elides it), sets the
// flush-to-zero mode, makes the four lanes' gains g0 = gain, g1 = g0 + s, g2 = g0 + 2s, g3 = g0 + (2s + s) and the step
// 4s = 2s + 2s, then for 16 blocks of 64 bytes loads the source's four vectors and the destination's four, computes
// dst_k = src_k * g_k + dst_k (vmaddfp as the generated code makes it: a multiply, then an add) and stores the four, and
// adds 4s to each gain. Here: the same operations on the same values, a block at a time in the same order (all of a
// block's loads before its stores: the same result where the channels overlap), each guest float byte-reversed once,
// the destination's store cards marked once for its 1024 bytes instead of at each vector store. Declined (the copy
// runs): a channel not 16-byte aligned, a range that is not one stretch of memory.
// Scratch not reproduced (hand.txt): r3-r10, v0-v13. r1 ends as it came.
namespace n_rex_XAudioMixFrameBufferChannel {

template <class S>
[[gnu::always_inline]] inline bool Native(PPCContext& __restrict ctx, uint8_t* base, [[maybe_unused]] S& s) {
  if (!On(kXAudioMix)) [[unlikely]] return false;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, gain = ctx.r5.u32 & ~15u, step = ctx.r6.u32 & ~15u;
  if (((dst | src) & 15u) || !detail::Straight(dst, 1024) || !detail::Straight(src, 1024) ||
      !detail::Straight(gain, 16) || !detail::Straight(step, 16)) [[unlikely]]
    return false;
  s.Hand(kXAudioMix, 256);
#if !defined(REX_NV_SAVES_ELIDE)
  {
    const uint32_t slot = ctx.r1.u32 - 8u;  // std r31,-8(r1): the local r31, zero
    if (!s.Note(slot, 8)) [[unlikely]] return true;
    std::memset(::banjo::hot::Raw(base, slot), 0, 8);
  }
#endif
  ctx.fpscr.enableFlushMode();
  if (!s.Note(dst, 1024)) [[unlikely]] return true;
  ::banjo::hot::Card(base, dst, 1024);
  uint8_t* d = ::banjo::hot::Raw(base, dst);
  const uint8_t* p = ::banjo::hot::Raw(base, src);
#if defined(__aarch64__)
  auto load = [](const uint8_t* at) { return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(at))); };
  auto store = [](uint8_t* at, float32x4_t v) { vst1q_u8(at, vrev32q_u8(vreinterpretq_u8_f32(v))); };
  const float32x4_t sv = load(::banjo::hot::Raw(base, step));
  const float32x4_t s2 = vaddq_f32(sv, sv);
  float32x4_t g0 = load(::banjo::hot::Raw(base, gain));
  float32x4_t g1 = vaddq_f32(g0, sv);
  float32x4_t g2 = vaddq_f32(g0, s2);
  float32x4_t g3 = vaddq_f32(g0, vaddq_f32(s2, sv));
  const float32x4_t s4 = vaddq_f32(s2, s2);
  for (uint32_t block = 0; block < 16; ++block, d += 64, p += 64) {
    const float32x4_t a0 = load(p), a1 = load(p + 16), a2 = load(p + 32), a3 = load(p + 48);
    const float32x4_t b0 = load(d), b1 = load(d + 16), b2 = load(d + 32), b3 = load(d + 48);
    store(d, vaddq_f32(vmulq_f32(a0, g0), b0));
    store(d + 16, vaddq_f32(vmulq_f32(a1, g1), b1));
    store(d + 32, vaddq_f32(vmulq_f32(a2, g2), b2));
    store(d + 48, vaddq_f32(vmulq_f32(a3, g3), b3));
    g0 = vaddq_f32(g0, s4);
    g1 = vaddq_f32(g1, s4);
    g2 = vaddq_f32(g2, s4);
    g3 = vaddq_f32(g3, s4);
  }
#else
  // PC: lane by lane, each a float multiply then an add (the build does not contract them).
  auto load = [](const uint8_t* at, float out[4]) {
    for (uint32_t k = 0; k < 4; ++k)
      out[k] = audio_filter::LoadF(at + 4 * k);
  };
  float sv[4], g[4][4], s4[4];
  load(::banjo::hot::Raw(base, step), sv);
  load(::banjo::hot::Raw(base, gain), g[0]);
  for (uint32_t k = 0; k < 4; ++k) {
    const float s2 = sv[k] + sv[k];
    g[1][k] = g[0][k] + sv[k];
    g[2][k] = g[0][k] + s2;
    g[3][k] = g[0][k] + (s2 + sv[k]);
    s4[k] = s2 + s2;
  }
  for (uint32_t block = 0; block < 16; ++block, d += 64, p += 64) {
    float a[4][4], b[4][4];
    for (uint32_t v = 0; v < 4; ++v) {
      load(p + 16 * v, a[v]);
      load(d + 16 * v, b[v]);
    }
    for (uint32_t v = 0; v < 4; ++v) {
      for (uint32_t k = 0; k < 4; ++k) {
        const float product = a[v][k] * g[v][k];
        audio_filter::StoreF(d + 16 * v + 4 * k, product + b[v][k]);
        g[v][k] = g[v][k] + s4[k];
      }
    }
  }
#endif
  return true;
}

}  // namespace n_rex_XAudioMixFrameBufferChannel

}  // namespace banjo::hand
