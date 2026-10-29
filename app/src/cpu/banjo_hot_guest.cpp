// banjo-nx - native replacements of hot guest functions (CPU enhancements banjo_cpu_native_*).
//
// The mechanism is sa2-nx's (app/src/cpu/sa2_hot_guest.cpp, GPL-3.0; it came there from masseffect-nx's
// me_hot_guest.cpp): each hook replaces one recompiled function by a native version and checks itself. Banjo's
// natives are all "localized copies" written by tools/localize_native.py (app/generated/hot/n_<id>.h, listed in
// app/src/cpu/hot/localized.txt): the recompiled function's own statements with its context registers in C++ locals.
//
// THE GUARD. For the first banjo_hot_guard_calls calls of each thread, one call in banjo_hot_guard_period afterwards
// and EVERY call with banjo_cpu_verify, a call is answered like this:
//   1. the copy's logged form runs on a copy of the registers: every guest store is recorded with the bytes it
//      replaces; the memory is rolled back from that log;
//   2. the ORIGINAL statements' logged form runs the same way and is rolled back;
//      (if either reached a call it cannot repeat - anything outside the copy: an indirect call, a kernel import, a
//      hooked function - the call is "not comparable": the recompiled original answers it in verify mode, the copy
//      otherwise, and it is counted;)
//   3. the bytes either of them wrote are the compare set: they are saved, the real copy runs on a copy of the
//      registers, its bytes are taken and the saved ones put back;
//   4. the recompiled original runs for real;
//   5. every register of the context, the written bytes and the host's flush mode are compared, and so are the
//      registers of the two logged runs.
// A difference that repeats when the whole pass is made again from the same inputs (three passes in a row) logs
// "[hot] DIFFERENCE ...", counts as a difference of the verify session (me::native::VerifyDifference) and switches
// that hook off for the rest of the run: the original had already produced the state. One that does not repeat came
// from another thread changing the inputs between the runs; it is counted apart ("did not repeat").
// A hook also stays off unless the loaded guest code is byte for byte the code its copy was made from.
//
// WHY EXACT: the copies are the same statements (tools/localize_native.py says what is renamed); the guard runs both.
// RISK: a difference on inputs the guard did not see; state another thread changes between the guard's runs is
//   reported as a difference (a false one) and only costs that hook.
// KEYS: banjo_cpu_native_<function> each (CPU group, under banjo_cpu_enhancements); banjo_hot_guard_calls,
//   banjo_hot_guard_period; banjo_hot_count (a measurement, see Counted).
//
// Name no guest function's address in this file: tools/direct_calls.py and tools/localize_native.py treat every
// 82xxxxxx address the app's sources mention as hooked. The list is app/src/cpu/hot/localized.txt.

// The generated code's macros and helpers: the copies are made of generated statements.
#include "renut_pch.h"

// banjo-nx (2026-10-19): whether the generated code marks the store cards (tools/store_cards.py ran and
// REX_STORE_CARDS_OFF is not set): Renut proves its cached copies by them only then (banjo_renut_store_cards).
#if defined(REX_CARD) && !defined(REX_STORE_CARDS_OFF)
extern "C" const bool g_banjo_store_cards = true;
#else
extern "C" const bool g_banjo_store_cards = false;
#endif

#include "banjo_hot_guest.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "cpu/banjo_enhancements.h"
#include "me/me_verify.h"

#include "cpu/hot/banjo_hand_natives.h"  // the hand-written natives the copies ask first (hot/hand.txt)
#include "hot/banjo_hot_all.h"  // app/generated/hot: the copies (tools/localize_native.py)

REXCVAR_DECLARE(bool, banjo_cpu_verify);
REXCVAR_DEFINE_INT32(banjo_hot_guard_calls, 64, "Banjo/Enhancements",
                     "Native copies of hot game functions: the first N guarded calls of each thread come one after "
                     "the other (each runs the copy and the recompiled original and compares them)")
    .range(0, 4000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_hot_guard_period, 4096, "Banjo/Enhancements",
                     "Native copies of hot game functions: after the first N, one call in P is guarded (rounded up to a "
                     "power of two, at most 1048576). 0 with banjo_hot_guard_calls = 0: no guard at all, every hook "
                     "calls its copy directly (no countdown; bejeweled3-nx's direct dispatch)")
    .range(0, 1 << 20)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// banjo-nx (2026-10-22, bejeweled3-nx's method): the bulk list's copies (app/src/cpu/hot/localized_bulk.txt, hooks
// in app/src/cpu/banjo_hot_shard_<k>.cpp) under one master key; each also has its own (category Banjo/Bulk copies).
REXCVAR_DEFINE_BOOL(banjo_cpu_native_bulk, true, "Banjo/Enhancements",
                    "Bulk localized copies: every game function the console profiles found hot that still ran as "
                    "generated code, as a localized copy (registers in C++ locals; tools/localize_native.py --bulk). "
                    "false = all of them back to the generated code (their own keys: banjo_cpu_native_sub_*)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_hot_count, false, "Banjo/Enhancements",
                    "Measurement (PC): every native copy runs in its counting form and logs/hot_counts.txt gets how "
                    "often each block of statements ran, for tools/hot_arm_compare.py. No guard while it is on")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The hand-written natives' own keys (app/src/cpu/hot/banjo_hand_natives.h; CPU group). Each stands in front of the
// generated copy of its function in every hook that contains it, so its key switches it off in all of them at once
// (the generated copies then run, as before).
REXCVAR_DEFINE_BOOL(banjo_hand_memcpy, true, "Banjo/Enhancements",
                    "Hand-written native of the game's C runtime memcpy (sa2-nx's): overlapping 16/8/4/2-byte moves "
                    "up to 128 bytes, the host's memcpy above; in front of the generated copy in every hook that "
                    "contains it. Declines overlapping forward smears; checked by those hooks' guards")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_hand_memset, true, "Banjo/Enhancements",
                    "Hand-written native of the game's C runtime memset (sa2-nx's): overlapping stores up to 64 "
                    "bytes, the host's memset above; checked by the guards of the hooks that contain it")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_hand_constants, true, "Banjo/Enhancements",
                    "Hand-written native of Direct3D's two shader float-constant writers (sa2-nx's): a plain copy of "
                    "the vectors and the dirty-mask update instead of the emulated unaligned vector loads; checked "
                    "by the guards of the hooks that contain them")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(banjo_hand_state_flush, true, "Banjo/Enhancements",
                    "Hand-written native of the two writers of Direct3D's state flush (dirty register words and "
                    "dirty float-constant blocks into PM4 packets in the command buffer): the runs are copied in "
                    "guest byte order instead of word by word through two byte swaps. Declines a run that needs "
                    "more buffer space; checked by the guards of the hooks that contain them")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(banjo_hand_audio_filter, true, "Banjo/Enhancements",
                    "Hand-written native of XAudio's per-voice state-variable filter (sub_82B0DCB8, the audio "
                    "thread's hottest guest function): the filter state in registers and four channels at a time "
                    "(NEON double lanes) instead of a byte-swapped load and store of every value at every sample; "
                    "bit-identical (the same double multiply-adds rounded to float), checked by the guard of its hook")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(banjo_hand_xaudio_mix, true, "Banjo/Enhancements",
                    "Hand-written native of XAudio's ramped channel mix (XAudio's MixFrameBufferChannel, the loop of "
                    "the audio thread's router: a voice's 256 samples times a gain ramp added to a submix channel): "
                    "the same vector multiplies and adds, sixteen samples a step with no store-card mark per vector "
                    "(one for the range); checked by the guard of the router's hook")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(banjo_hand_transforms, true, "Banjo/Enhancements",
                    "nights-nx: hand-written natives of NiGHTS's two matrix-stack transforms (sub_822D9CB0, a point "
                    "times the top matrix divided by its w; sub_822D9DD8, a direction times its 3x3): the same scalar "
                    "multiplies and adds in the same order on twelve bytes in and out instead of the emulated "
                    "unaligned vector loads; checked by the guards of the hooks that contain them")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(banjo_hand_buffer_reset, true, "Banjo/Enhancements",
                    "nights-nx: hand-written native of NiGHTS's float-buffer reset (sub_823EAFD8, the second game "
                    "thread's hottest function: up to 16 buffers set to one float and then another): sixteen bytes a "
                    "store and one store-card mark per buffer instead of a store, a mark and two loads per float; "
                    "declines buffers that meet the block; checked by the guards of the hooks that contain it")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::hand {

uint8_t g_state[kNatives] = {};

namespace {
constexpr const char* kNames[kNatives] = {"memcpy", "memset", "constant writers", "state flush writers",
                                          "XAudio voice filter", "XAudio channel mix", "matrix-stack transforms", "float-buffer reset"};
constexpr const char* kKeys[kNatives] = {"banjo_hand_memcpy", "banjo_hand_memset", "banjo_hand_constants",
                                         "banjo_hand_state_flush", "banjo_hand_audio_filter",
                                         "banjo_hand_xaudio_mix", "banjo_hand_transforms",
                                         "banjo_hand_buffer_reset"};
bool Own(uint32_t which) {
  return which == kMemcpy        ? bool(REXCVAR_GET(banjo_hand_memcpy))
         : which == kMemset      ? bool(REXCVAR_GET(banjo_hand_memset))
         : which == kConstants   ? bool(REXCVAR_GET(banjo_hand_constants))
         : which == kFlush       ? bool(REXCVAR_GET(banjo_hand_state_flush))
         : which == kAudioFilter ? bool(REXCVAR_GET(banjo_hand_audio_filter))
         : which == kXAudioMix   ? bool(REXCVAR_GET(banjo_hand_xaudio_mix))
         : which == kTransforms  ? bool(REXCVAR_GET(banjo_hand_transforms))
                                 : bool(REXCVAR_GET(banjo_hand_buffer_reset));
}
// banjo_hot_count: calls by size, bucket 0 = size 0, bucket k = sizes [2^(k-1), 2^k).
std::atomic<uint64_t> g_sizes[kNatives][34];
}  // namespace

// May run on several threads at once for the first call: every one of them stores the same value.
uint8_t Init(uint32_t which) {
  const uint8_t state = banjo::EnhancementActive(banjo::EnhancementGroup::kCpu, Own(which)) ? 2 : 1;
  if (g_state[which] == 0)
    REXLOG_INFO("[hot] hand-written native of the {} ({}): {}", kNames[which], kKeys[which], state == 2 ? "ON" : "off");
  g_state[which] = state;
  return state;
}

void Count(uint32_t which, uint32_t n) {
  g_sizes[which][n ? 32 - __builtin_clz(n) : 0].fetch_add(1, std::memory_order_relaxed);
}

}  // namespace banjo::hand

namespace banjo::hot {

namespace {

Hook* g_hooks = nullptr;  // every hook (static initialisation, one thread)

// The buffers of one guarded call. A guarded call can contain others (the recompiled original calls hooked
// functions, itself among them), so a thread has one of these per nesting depth. Reached only from Check: the
// thread_local costs the guarded calls alone.
struct Scratch {
  WriteLog log;
  std::vector<Region> ranges;
  std::vector<uint8_t> before;
  std::vector<uint8_t> native_out;
  PPCContext in;  // the registers the call came with
  size_t bytes = 0;
};
struct ThreadScratch {
  std::vector<Scratch*> frames;  // live as long as the thread (guest threads are few and long-lived)
  uint32_t depth = 0;
};
ThreadScratch& Thread() {
  static thread_local ThreadScratch* scratch = nullptr;
  if (!scratch) scratch = new ThreadScratch;
  return *scratch;
}
struct ScratchFrame {
  ThreadScratch& thread;
  Scratch& scratch;
  explicit ScratchFrame(ThreadScratch& t) : thread(t), scratch(Take(t)) {}
  ~ScratchFrame() {
    --thread.depth;
    // A guarded call that wrote megabytes (a memset of a loader's buffer among a thread's first calls) leaves its
    // log and its two copies of those bytes behind, about four times what it wrote, for as long as the thread
    // lives. On the console that memory is the same that textures and the driver need: large ones are given back.
    constexpr size_t kKeepBytes = 256u << 10;
    if (size_t(scratch.log.capacity) * sizeof(WriteLog::Entry) > kKeepBytes) {
      std::free(scratch.log.entries);
      scratch.log.entries = nullptr;
      scratch.log.capacity = scratch.log.count = 0;
    }
    if (scratch.ranges.capacity() * sizeof(Region) > kKeepBytes) std::vector<Region>().swap(scratch.ranges);
    if (scratch.before.capacity() > kKeepBytes) std::vector<uint8_t>().swap(scratch.before);
    if (scratch.native_out.capacity() > kKeepBytes) std::vector<uint8_t>().swap(scratch.native_out);
  }
  static Scratch& Take(ThreadScratch& t) {
    if (t.depth == t.frames.size()) t.frames.push_back(new Scratch);
    return *t.frames[t.depth++];
  }
};

bool GuestCodeMatches(const Hook& h, const uint8_t* base) {
  uint64_t fnv = 0xCBF29CE484222325ull;
  for (uint32_t r = 0; r < h.code_ranges; ++r) {
    const uint8_t* p = base + h.code[r].address;
    for (uint32_t i = 0; i < h.code[r].bytes; ++i) {
      fnv ^= p[i];
      fnv *= 0x100000001B3ull;
    }
  }
  if (fnv == h.code_hash) return true;
  REXLOG_WARN("[hot] {} ({}): the game code at {:08X} is not the code the copy was made from (hash {:016X}, expected "
              "{:016X}); it stays off",
              h.name, h.key, h.code[0].address, fnv, h.code_hash);
  return false;
}

const PPCRegister* Gpr(const PPCContext& c, int n) {
  switch (n) {
#define G(i) \
  case i:    \
    return &c.r##i;
    G(0) G(1) G(2) G(3) G(4) G(5) G(6) G(7) G(8) G(9) G(10) G(11) G(12) G(13) G(14) G(15) G(16) G(17) G(18) G(19)
    G(20) G(21) G(22) G(23) G(24) G(25) G(26) G(27) G(28) G(29) G(30) G(31)
#undef G
  }
  return nullptr;
}
const PPCRegister* Fpr(const PPCContext& c, int n) { return &c.f0 + n; }    // f0..f31 are consecutive
const PPCVRegister* Vr(const PPCContext& c, int n) { return &c.v0 + n; }    // v0..v127 are consecutive
const PPCCRRegister* Cr(const PPCContext& c, int n) { return &c.cr0 + n; }  // cr0..cr7 are consecutive

// First difference between two contexts (every register but the hook's skip set), or an empty string. a: the copy;
// b: the original.
std::string CompareRegisters(const PPCContext& a, const PPCContext& b, const RegSkip& skip) {
  for (int i = 0; i < 32; ++i) {
    if ((skip.gpr >> i) & 1) continue;
    if (Gpr(a, i)->u64 != Gpr(b, i)->u64)
      return fmt::format("r{} copy={:#018x} original={:#018x}", i, Gpr(a, i)->u64, Gpr(b, i)->u64);
  }
  for (int i = 0; i < 32; ++i) {
    if ((skip.fpr >> i) & 1) continue;
    if (Fpr(a, i)->u64 != Fpr(b, i)->u64)
      return fmt::format("f{} copy={:#018x} original={:#018x}", i, Fpr(a, i)->u64, Fpr(b, i)->u64);
  }
  for (int i = 0; i < 128; ++i) {
    if ((skip.vr[i >> 6] >> (i & 63)) & 1) continue;
    if (std::memcmp(Vr(a, i)->u8, Vr(b, i)->u8, 16) != 0) {
      const auto* x = Vr(a, i)->u32;
      const auto* y = Vr(b, i)->u32;
      return fmt::format("v{} copy={:08x}{:08x}{:08x}{:08x} original={:08x}{:08x}{:08x}{:08x}", i, x[0], x[1], x[2],
                         x[3], y[0], y[1], y[2], y[3]);
    }
  }
  for (int i = 0; i < 8; ++i) {
    if ((skip.cr >> i) & 1) continue;
    if (Cr(a, i)->raw() != Cr(b, i)->raw())
      return fmt::format("cr{} copy={:x} original={:x}", i, Cr(a, i)->raw(), Cr(b, i)->raw());
  }
  if (!skip.ctr && a.ctr.u64 != b.ctr.u64) return fmt::format("ctr copy={:#x} original={:#x}", a.ctr.u64, b.ctr.u64);
  if (!skip.xer && (a.xer.so != b.xer.so || a.xer.ov != b.xer.ov || a.xer.ca != b.xer.ca)) return "xer";
  if (a.reserved.u64 != b.reserved.u64) return "reserved";
  if (a.lr != b.lr) return fmt::format("lr copy={:#x} original={:#x}", a.lr, b.lr);
  if (a.vscr_sat != b.vscr_sat) return "vscr_sat";
  // Only the guest-visible bits (rounding, flush): the host's sticky exception flags may differ harmlessly.
  constexpr uint32_t kGuest = rex::ppc::FPSCRRegister::GuestMask;
  if ((a.fpscr.csr & kGuest) != (b.fpscr.csr & kGuest))
    return fmt::format("fpscr.csr copy={:#x} original={:#x}", a.fpscr.csr, b.fpscr.csr);
  return {};
}

void AddRanges(const WriteLog& log, std::vector<Region>& ranges) {
  for (uint32_t i = 0; i < log.count; ++i) ranges.push_back({log.entries[i].addr, log.entries[i].len});
}

// Sorted, without overlaps.
void Merge(std::vector<Region>& ranges) {
  std::sort(ranges.begin(), ranges.end(), [](const Region& a, const Region& b) { return a.addr < b.addr; });
  size_t n = 0;
  for (const Region& r : ranges) {
    if (n && uint64_t(ranges[n - 1].addr) + ranges[n - 1].len >= r.addr) {
      const uint64_t end = std::max(uint64_t(ranges[n - 1].addr) + ranges[n - 1].len, uint64_t(r.addr) + r.len);
      ranges[n - 1].len = uint32_t(end - ranges[n - 1].addr);
    } else {
      ranges[n++] = r;
    }
  }
  ranges.resize(n);
}

std::atomic<int64_t> g_last_report_ms{0};
int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void WriteCounts() {
  FILE* f = std::fopen("logs/hot_counts.txt", "w");
  if (!f) return;
  std::fprintf(f, "# banjo_hot_count: <function> <block> <entries>   (tools/hot_arm_compare.py)\n");
  for (Hook* h = g_hooks; h; h = h->next) {
    for (uint32_t i = 0; i < h->hit_count; ++i) {
      const uint64_t n = h->hits[i].load(std::memory_order_relaxed);
      if (n) std::fprintf(f, "%s %u %llu\n", h->name, i, static_cast<unsigned long long>(n));
    }
  }
  std::fclose(f);
}

}  // namespace

Hook::Hook(const char* name_, const char* key_, PPCFunc* orig_, void (*native_)(PPCContext&, uint8_t*),
           void (*native_logged_)(PPCContext&, uint8_t*, Logged&),
           void (*reference_logged_)(PPCContext&, uint8_t*, Logged&), void (*native_counted_)(PPCContext&, uint8_t*),
           std::atomic<uint64_t>* hits_, uint32_t hit_count_, const CodeRange* code_, uint32_t code_ranges_,
           uint64_t code_hash_, bool (*own_)(), uint32_t slot_, const RegSkip* skip_, bool bulk_)
    : name(name_),
      key(key_),
      orig(orig_),
      native(native_),
      native_logged(native_logged_),
      reference_logged(reference_logged_),
      native_counted(native_counted_),
      hits(hits_),
      hit_count(hit_count_),
      code(code_),
      code_ranges(code_ranges_),
      code_hash(code_hash_),
      own(own_),
      slot(slot_),
      skip(skip_),
      bulk(bulk_) {
  // In list order: the hooks are defined in the order of banjo_hot_list.inc, so append.
  Hook** tail = &g_hooks;
  while (*tail) tail = &(*tail)->next;
  *tail = this;
}

bool WriteLog::Grow() {
  if (capacity >= kMaxEntries) return false;
  const uint32_t wanted = capacity ? capacity * 2 : 4096;
  auto* grown = static_cast<Entry*>(std::realloc(entries, size_t(wanted) * sizeof(Entry)));
  if (!grown) return false;
  entries = grown;
  capacity = wanted;
  return true;
}

uint8_t InitHook(Hook& h, uint8_t* base) {
  // May run on several threads at once for the first call: every one of them stores the same values.
  bool on = banjo::EnhancementActive(banjo::EnhancementGroup::kCpu, h.own());
  if (on && h.bulk && !REXCVAR_GET(banjo_cpu_native_bulk)) on = false;
  if (on && !GuestCodeMatches(h, base)) on = false;
  const bool verify = REXCVAR_GET(banjo_cpu_verify);
  uint32_t period = 1;
  while (period < uint32_t(std::max(1, REXCVAR_GET(banjo_hot_guard_period))) && period < (1u << kCountdownBits))
    period <<= 1;
  h.verify = verify;
  h.guard_calls = uint32_t(std::clamp(REXCVAR_GET(banjo_hot_guard_calls), 0, 4000));
  h.period = period;
  // No guard at all: the hook calls its copy directly (banjo_hot_guest.h, kDirect).
  const bool direct = !verify && h.guard_calls == 0 && REXCVAR_GET(banjo_hot_guard_period) == 0;
  uint8_t state = on ? (direct ? kDirect : kOn) : kOff;
  if (on && REXCVAR_GET(banjo_hot_count) && h.native_counted) state = kCount;
  if (on && h.bulk) {
    // The bulk hooks say so in the summary of the reports (hundreds of first calls would each be a line).
  } else if (on && state == kDirect && h.state.load(std::memory_order_relaxed) == kUninit) {
    REXLOG_INFO("[hot] {} ({}): native copy ON, direct (no guard: banjo_hot_guard_calls = banjo_hot_guard_period = 0)",
                h.name, h.key);
  } else if (on && h.state.load(std::memory_order_relaxed) == kUninit) {
    if (state == kCount) {
      REXLOG_INFO("[hot] {} ({}): native copy ON in its counting form (banjo_hot_count; no guard)", h.name, h.key);
    } else if (verify) {
      REXLOG_INFO("[hot] {} ({}): native copy ON, verify mode (every call runs both and compares)", h.name, h.key);
    } else {
      REXLOG_INFO("[hot] {} ({}): native copy ON (guard: the first {} calls of a thread, then 1 in {})", h.name, h.key,
                  h.guard_calls, period);
    }
  }
  h.state.store(state, std::memory_order_release);
  return state;
}

void Counted(Hook& h, PPCContext& ctx, uint8_t* base) {
  h.native_counted(ctx, base);
  if ((h.uses.fetch_add(1, std::memory_order_relaxed) & 0xFFF) == 0) Report();
}

namespace {

enum class Outcome { kEqual, kDifferent, kNotComparable, kPathsDiffer };

// One pass of the guard (the steps at the top of this file). kEqual and kDifferent: the recompiled original has run
// for real and `diff` says what differed. kNotComparable / kPathsDiffer: nothing has run for real yet.
Outcome Attempt(Hook& h, PPCContext& ctx, uint8_t* base, Scratch& t, std::string& diff) {
  const uint32_t hw_before = ctx.fpscr.getcsr();
  const uint32_t csr_before = ctx.fpscr.csr;
  t.ranges.clear();
  t.in = ctx;

  // 1 and 2: what would be written? Both logged forms, each rolled back.
  PPCContext logged_native = ctx;
  t.log.Reset(base);
  {
    Logged s{t.log};
    h.native_logged(logged_native, base, s);
  }
  const bool native_stopped = t.log.stopped;
  bool overflow = t.log.overflow;
  AddRanges(t.log, t.ranges);
  t.log.RollBack();
  ctx.fpscr.setcsr(hw_before);

  PPCContext logged_reference = ctx;
  t.log.Reset(base);
  {
    Logged s{t.log};
    h.reference_logged(logged_reference, base, s);
  }
  const bool reference_stopped = t.log.stopped;
  overflow |= t.log.overflow;
  AddRanges(t.log, t.ranges);
  t.log.RollBack();
  ctx.fpscr.setcsr(hw_before);
  ctx.fpscr.csr = csr_before;

  if (native_stopped || reference_stopped) {
    // A call the guard cannot repeat, or a log past its limit. One of the two stopping and the other not means
    // they took different paths.
    if (native_stopped != reference_stopped && !overflow) {
      diff = fmt::format("the copy {} a call outside itself and the original statements {}",
                         native_stopped ? "reached" : "did not reach", reference_stopped ? "did" : "did not");
      return Outcome::kPathsDiffer;
    }
    return Outcome::kNotComparable;
  }

  // 3: the compare set, saved; the real copy on a copy of the registers; its bytes out, the saved ones back.
  Merge(t.ranges);
  size_t total = 0;
  for (const Region& r : t.ranges) total += r.len;
  t.bytes = total;
  t.before.resize(total);
  t.native_out.resize(total);
  size_t off = 0;
  for (const Region& r : t.ranges) {
    std::memcpy(t.before.data() + off, Raw(base, r.addr), r.len);
    off += r.len;
  }
  PPCContext nat = ctx;
  h.native(nat, base);
  const uint32_t hw_native = nat.fpscr.getcsr();
  off = 0;
  for (const Region& r : t.ranges) {
    std::memcpy(t.native_out.data() + off, Raw(base, r.addr), r.len);
    std::memcpy(Raw(base, r.addr), t.before.data() + off, r.len);
    off += r.len;
  }
  ctx.fpscr.setcsr(hw_before);
  ctx.fpscr.csr = csr_before;

  // 4: the call for real. (It may contain guarded calls of its own: they use the next scratch frame.)
  h.orig(ctx, base);
  const uint32_t hw_orig = ctx.fpscr.getcsr();

  // 5: compare.
  diff = CompareRegisters(nat, ctx, *h.skip);
  if (diff.empty() &&
      (hw_native & rex::ppc::FPSCRRegister::GuestMask) != (hw_orig & rex::ppc::FPSCRRegister::GuestMask))
    diff = fmt::format("host flush mode copy={:#x} original={:#x}", hw_native, hw_orig);
  if (diff.empty()) {
    off = 0;
    for (const Region& r : t.ranges) {
      const uint8_t* now = Raw(base, r.addr);
      if (std::memcmp(now, t.native_out.data() + off, r.len) != 0) {
        uint32_t k = 0;
        while (now[k] == t.native_out[off + k]) ++k;
        diff = fmt::format("memory {:#010x} (+{} of the written range {:#010x}+{}) copy={:#04x} original={:#04x}",
                           r.addr + k, k, r.addr, r.len, t.native_out[off + k], now[k]);
        break;
      }
      off += r.len;
    }
  }
  if (diff.empty()) {
    diff = CompareRegisters(logged_native, ctx, *h.skip);
    if (!diff.empty()) diff = "the copy's logged form: " + diff;
  }
  if (diff.empty()) {
    // (The skip set here too: the recompiled original that just ran reaches the hand-written natives through the
    // hooks of their functions, so it has their registers, not the original statements'.)
    diff = CompareRegisters(logged_reference, ctx, *h.skip);
    if (!diff.empty()) diff = "the original statements' logged form: " + diff;
  }
  return diff.empty() ? Outcome::kEqual : Outcome::kDifferent;
}

}  // namespace

void Check(Hook& h, PPCContext& ctx, uint8_t* base, uint32_t& counter) {
  // The next guarded call of this thread: at once while it is still in its first guard_calls (or in verify mode),
  // else after `period` calls. The counter word: banjo_hot_guest.h.
  const uint32_t phase = counter >> kCountdownBits;
  if (h.verify) {
    counter = 0;
    h.uses.fetch_add(1, std::memory_order_relaxed);
  } else if (phase < h.guard_calls) {
    counter = (phase + 1) << kCountdownBits;
    h.uses.fetch_add(1, std::memory_order_relaxed);
  } else {
    counter = (phase << kCountdownBits) | (h.period - 1);
    h.uses.fetch_add(h.period, std::memory_order_relaxed);
  }
  Report();  // at most every 10 s
  if (h.state.load(std::memory_order_relaxed) != kOn) {  // switched off by another thread's check meanwhile
    h.orig(ctx, base);
    return;
  }

  ScratchFrame frame(Thread());
  Scratch& t = frame.scratch;
  const uint32_t hw_entry = ctx.fpscr.getcsr();
  std::string diff;
  // A difference must repeat to count. The copy and the original are functions of the registers and of guest
  // memory; when another thread changes memory they read between the guard's runs (the audio threads read buffers
  // the decoder is filling), the runs differ without either being wrong. So on a difference the inputs are put back
  // (the registers the call came with, the bytes it wrote) and the whole pass is made again, twice at most: a wrong
  // copy differs every time, a race does not.
  constexpr int kAttempts = 3;
  for (int attempt = 1;; ++attempt) {
    const Outcome outcome = Attempt(h, ctx, base, t, diff);
    if (outcome == Outcome::kNotComparable) {
      h.not_comparable.fetch_add(1, std::memory_order_relaxed);
      if (h.verify) {
        h.orig(ctx, base);
      } else {
        h.native(ctx, base);
      }
      return;
    }
    if (outcome == Outcome::kEqual) {
      h.compared.fetch_add(1, std::memory_order_relaxed);
      ::me::native::g_verify_native_checks.fetch_add(1, std::memory_order_relaxed);  // the "[verify]" line
      if (attempt > 1) h.not_repeated.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (attempt < kAttempts) {
      if (outcome == Outcome::kDifferent) {  // the original has run: put its inputs back
        ctx = t.in;
        size_t off = 0;
        for (const Region& r : t.ranges) {
          std::memcpy(Raw(base, r.addr), t.before.data() + off, r.len);
          off += r.len;
        }
        ctx.fpscr.setcsr(hw_entry);
      }
      continue;
    }
    // It repeated.
    h.compared.fetch_add(1, std::memory_order_relaxed);
    ::me::native::g_verify_native_checks.fetch_add(1, std::memory_order_relaxed);
    h.differences.fetch_add(1, std::memory_order_relaxed);
    h.state.store(kOff, std::memory_order_relaxed);
    ::me::native::VerifyDifference();
    const PPCContext& in = t.in;
    REXLOG_ERROR("[hot] DIFFERENCE {} ({}): {} ({} passes in a row); inputs r3={:#x} r4={:#x} r5={:#x} r6={:#x} "
                 "r7={:#x} f1={:#x} f2={:#x}; {} bytes in {} written ranges; native copy switched off for the rest of "
                 "the run",
                 h.name, h.key, diff, kAttempts, in.r3.u64, in.r4.u64, in.r5.u64, in.r6.u64, in.r7.u64, in.f1.u64,
                 in.f2.u64, t.bytes, t.ranges.size());
    if (outcome == Outcome::kPathsDiffer) h.orig(ctx, base);  // nothing has run for real yet
    return;
  }
}

void ForEachKey(void (*row)(void* user, const char* key, bool own), void* user) {
  bool bulk = false;
  for (Hook* h = g_hooks; h; h = h->next) {
    if (!h->bulk) {
      row(user, h->key, h->own());
    } else {
      bulk = true;
      if (!h->own()) row(user, h->key, false);  // a bulk copy switched off by its own key
    }
  }
  if (bulk) row(user, "banjo_cpu_native_bulk", REXCVAR_GET(banjo_cpu_native_bulk));
  for (uint32_t k = 0; k < ::banjo::hand::kNatives; ++k) row(user, ::banjo::hand::kKeys[k], ::banjo::hand::Own(k));
}

void Report() {
  const int64_t now = NowMs();
  int64_t last = g_last_report_ms.load(std::memory_order_relaxed);
  if (now - last < 10000 || !g_last_report_ms.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
  if (REXCVAR_GET(banjo_hot_count)) {
    WriteCounts();
    // The hand-written natives' calls by size (for an estimate of what they cost on the A57).
    for (uint32_t k = 0; k < ::banjo::hand::kNatives; ++k) {
      std::string sizes;
      uint64_t calls = 0;
      for (uint32_t b = 0; b < 34; ++b) {
        const uint64_t n = ::banjo::hand::g_sizes[k][b].load(std::memory_order_relaxed);
        if (!n) continue;
        calls += n;
        sizes += b == 0 ? fmt::format(" [0: {}]", n) : fmt::format(" [{}-{}: {}]", 1u << (b - 1), (1ull << b) - 1, n);
      }
      if (calls)
        REXLOG_INFO("[hot] hand-written native of the {}: {} calls so far, by size ({}):{}", ::banjo::hand::kNames[k],
                    calls,
                    k == ::banjo::hand::kConstants ? "vectors" : k == ::banjo::hand::kFlush ? "words written" : "bytes",
                    sizes);
    }
    return;
  }
  // One line per hook that was reached, so each fits the log's line and a tool reads one function per line.
  uint64_t compared = 0, not_comparable = 0, differences = 0, not_repeated = 0;
  uint32_t reached = 0, on = 0, direct = 0, bulk_total = 0, bulk_reached = 0, bulk_on = 0;
  for (Hook* h = g_hooks; h; h = h->next) {
    const uint8_t state = h->state.load(std::memory_order_relaxed);
    bulk_total += h->bulk;
    if (state == kUninit) continue;
    ++reached;
    on += state == kOn || state == kDirect;
    direct += state == kDirect;
    if (h->bulk) {
      ++bulk_reached;
      bulk_on += state == kOn || state == kDirect;
      compared += h->compared.load(std::memory_order_relaxed);
      not_comparable += h->not_comparable.load(std::memory_order_relaxed);
      not_repeated += h->not_repeated.load(std::memory_order_relaxed);
      const uint64_t d = h->differences.load(std::memory_order_relaxed);
      differences += d;
      // A bulk hook has a line of its own only when it found a difference (its copy is off since).
      if (d)
        REXLOG_INFO("[hot] {} (bulk) off after {} difference(s); {} calls compared before", h->name, d,
                    h->compared.load(std::memory_order_relaxed));
      continue;
    }
    const uint64_t c = h->compared.load(std::memory_order_relaxed);
    const uint64_t n = h->not_comparable.load(std::memory_order_relaxed);
    const uint64_t d = h->differences.load(std::memory_order_relaxed);
    compared += c;
    not_comparable += n;
    differences += d;
    const uint64_t r = h->not_repeated.load(std::memory_order_relaxed);
    not_repeated += r;
    if (state == kDirect) continue;  // nothing is counted without the guard: the summary says it is on
    REXLOG_INFO("[hot] {} {}: about {} calls so far, {} compared with the original, {} not comparable (they reach a "
                "call outside the copy), {} differences, {} more that did not repeat (inputs changed by another thread)",
                h->name, state == kOn ? "ON" : "off", h->uses.load(std::memory_order_relaxed), c, n, d, r);
  }
  REXLOG_INFO("[hot] native copies: {} reached, {} on ({} direct, no guard); bulk copies (banjo_cpu_native_bulk {}): {} "
              "of {} reached, {} on; {} calls compared so far, {} not comparable, {} differed once and not when "
              "repeated, differences: {}",
              reached, on, direct, REXCVAR_GET(banjo_cpu_native_bulk) ? "on" : "off", bulk_reached, bulk_total, bulk_on,
              compared, not_comparable, not_repeated, differences);
}

}  // namespace banjo::hot

// The hooks (tools/localize_native.py writes one BANJO_HOT_HOOK line per function of localized.txt).
#include "hot/banjo_hot_list.inc"
