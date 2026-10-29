// banjo-nx - native replacements of hot guest functions: the hook in front of each one, with its self-check guard.
//
// From sa2-nx's sa2_hot_guest.h (GPL-3.0; itself from masseffect-nx's me_hot_guest.h), see banjo_hot_common.h.
//
// The cost of the hook is paid on every call of a hot function, so the path to the copy is: one byte (is this hook on),
// one counter in the calling thread's own context (PPCContext::host_counters: no thread_local, which on the Switch is
// a call to __aarch64_read_tp, and no counter shared between threads), one test, the copy. The counter counts down to
// the next guarded call: Check() refills it.
//
// banjo-nx (2026-10-22, bejeweled3-nx's method): with no guard at all (banjo_hot_guard_calls = 0 and
// banjo_hot_guard_period = 0), verify and banjo_hot_count off, a hook's state is kDirect and the path is one byte and
// the copy: no countdown (for the bulk hooks a counter shared by every thread, written at every call).
#pragma once

#include <atomic>
#include <cstdint>
#include <iterator>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>

#include "cpu/banjo_hot_common.h"

namespace banjo::hot {

enum : uint8_t { kUninit = 0, kOff = 1, kOn = 2, kCount = 3, kDirect = 4 };

// PPCContext::host_counters slots for the hooks (tools/localize_native.py numbers them in list order); a hook past
// them shares one counter between threads.
inline constexpr uint32_t kCtxSlots = 60;
// A counter word: the low 20 bits count down to the next guarded call of this thread, the high 12 count the guarded
// calls it has made while that is still below banjo_hot_guard_calls.
inline constexpr uint32_t kCountdownBits = 20;
inline constexpr uint32_t kCountdownMask = (1u << kCountdownBits) - 1;

struct Hook {
  // Fixed (the BANJO_HOT_HOOK line).
  const char* name;  // "sub_82949790"
  const char* key;   // its cvar
  PPCFunc* orig;     // the recompiled function
  void (*native)(PPCContext&, uint8_t*);
  void (*native_logged)(PPCContext&, uint8_t*, Logged&);
  void (*reference_logged)(PPCContext&, uint8_t*, Logged&);
  void (*native_counted)(PPCContext&, uint8_t*);  // banjo_hot_count (a measurement)
  std::atomic<uint64_t>* hits;                    // ... its block counters
  uint32_t hit_count;
  const CodeRange* code;  // the guest code the copy was made from
  uint32_t code_ranges;
  uint64_t code_hash;
  bool (*own)();  // reads the key
  uint32_t slot;
  const RegSkip* skip;  // registers the guard does not compare (hand-written natives in the copy)
  bool bulk;            // a hook of the bulk list (localized_bulk.txt): one master key, a summary in the reports
  Hook* next = nullptr;  // every hook, for the reports

  // Run time. Zero until the first call: kUninit sends that call to InitHook.
  std::atomic<uint8_t> state{kUninit};
  uint32_t guard_calls = 0;  // the first N guarded calls of each thread come one after the other ...
  uint32_t period = 1;       // ... afterwards one call in `period`
  bool verify = false;       // banjo_cpu_verify: every call
  std::atomic<uint32_t> shared_counter{0};  // slot >= kCtxSlots
  std::atomic<uint64_t> uses{0};            // calls the copy answered (estimated from the guard's period)
  std::atomic<uint64_t> compared{0};        // guarded calls that ran both versions and compared them
  std::atomic<uint64_t> not_comparable{0};  // guarded calls that reached a call the guard cannot repeat
  std::atomic<uint64_t> differences{0};
  std::atomic<uint64_t> not_repeated{0};    // guarded calls that differed once and compared equal when repeated

  Hook(const char* name_, const char* key_, PPCFunc* orig_, void (*native_)(PPCContext&, uint8_t*),
       void (*native_logged_)(PPCContext&, uint8_t*, Logged&), void (*reference_logged_)(PPCContext&, uint8_t*, Logged&),
       void (*native_counted_)(PPCContext&, uint8_t*), std::atomic<uint64_t>* hits_, uint32_t hit_count_,
       const CodeRange* code_, uint32_t code_ranges_, uint64_t code_hash_, bool (*own_)(), uint32_t slot_,
       const RegSkip* skip_, bool bulk_);
};

// First call of a hook (any thread): its key and the CPU master switch, the guest code check, the guard's settings.
// Returns the state. banjo_hot_guest.cpp.
uint8_t InitHook(Hook& h, uint8_t* base);

// One guarded call. Leaves registers and memory exactly as the recompiled original leaves them.
void Check(Hook& h, PPCContext& ctx, uint8_t* base, uint32_t& counter);

// banjo_hot_count: the copy with its block counters (tools/hot_arm_compare.py reads them).
void Counted(Hook& h, PPCContext& ctx, uint8_t* base);

// The keys, for the "[enhancements]" line (banjo_enhancements.cpp): calls `row(key, own key on)` for each.
void ForEachKey(void (*row)(void* user, const char* key, bool own), void* user);

// The counters of every hook that was reached, once per report of the renderer (10 s) and at most that often.
void Report();

}  // namespace banjo::hot

// The counting form is a PC measurement (banjo_hot_count): the Switch build does not make it (nor its code).
#if REX_PLATFORM_SWITCH
#define BANJO_HOT_COUNTED(ID) nullptr
#else
#define BANJO_HOT_COUNTED(ID) &n_##ID::NativeCounted
#endif

// SYM: the generated function's name; ID: the namespace suffix of its header (n_<ID>); KEY: its cvar; SLOT: its
// PPCContext::host_counters slot. Written by tools/localize_native.py (app/generated/hot/banjo_hot_list.inc, and
// banjo_hot_bulk_<k>.inc for the bulk list: BANJO_HOT_HOOK_BULK, its keys in a category of their own).
#define BANJO_HOT_HOOK(SYM, ID, KEY, DEFAULT, SLOT, DESC) \
  BANJO_HOT_HOOK_EX(SYM, ID, KEY, DEFAULT, SLOT, DESC, false, "Banjo/Enhancements")
#define BANJO_HOT_HOOK_BULK(SYM, ID, KEY, DEFAULT, SLOT, DESC) \
  BANJO_HOT_HOOK_EX(SYM, ID, KEY, DEFAULT, SLOT, DESC, true, "Banjo/Bulk copies")
#define BANJO_HOT_HOOK_EX(SYM, ID, KEY, DEFAULT, SLOT, DESC, BULK, CATEGORY)                                     \
  REXCVAR_DEFINE_BOOL(KEY, DEFAULT, CATEGORY, DESC).lifecycle(rex::cvar::Lifecycle::kInitOnly);                  \
  REX_EXTERN(__imp__##SYM);                                                                                      \
  namespace banjo::hot {                                                                                         \
  Hook g_hook_##ID{#SYM, #KEY, &__imp__##SYM, &n_##ID::Native, &n_##ID::NativeLogged, &n_##ID::ReferenceLogged,  \
                   BANJO_HOT_COUNTED(ID), n_##ID::g_hits, uint32_t(std::size(n_##ID::g_hits)), n_##ID::kCode,    \
                   uint32_t(std::size(n_##ID::kCode)), n_##ID::kCodeHash, [] { return bool(REXCVAR_GET(KEY)); }, \
                   SLOT, &n_##ID::kSkip, BULK};                                                                  \
  }                                                                                                              \
  REX_HOOK_RAW(SYM) {                                                                                            \
    banjo::hot::Hook& h = banjo::hot::g_hook_##ID;                                                               \
    uint8_t st = h.state.load(std::memory_order_relaxed);                                                        \
    if (st == banjo::hot::kDirect) [[likely]] {                                                                  \
      banjo::hot::n_##ID::Native(ctx, base);                                                                     \
      return;                                                                                                    \
    }                                                                                                            \
    if (st != banjo::hot::kOn) [[unlikely]] {                                                                    \
      if (st == banjo::hot::kUninit) st = banjo::hot::InitHook(h, base);                                         \
      if (st == banjo::hot::kDirect) {                                                                           \
        banjo::hot::n_##ID::Native(ctx, base);                                                                   \
        return;                                                                                                  \
      }                                                                                                          \
      if (st == banjo::hot::kCount) {                                                                            \
        banjo::hot::Counted(h, ctx, base);                                                                       \
        return;                                                                                                  \
      }                                                                                                          \
      if (st != banjo::hot::kOn) {                                                                               \
        __imp__##SYM(ctx, base);                                                                                 \
        return;                                                                                                  \
      }                                                                                                          \
    }                                                                                                            \
    if constexpr ((SLOT) < banjo::hot::kCtxSlots) {                                                              \
      uint32_t& counter = ctx.host_counters[SLOT];                                                               \
      if ((counter & banjo::hot::kCountdownMask) == 0) [[unlikely]] {                                            \
        banjo::hot::Check(h, ctx, base, counter);                                                                \
        return;                                                                                                  \
      }                                                                                                          \
      --counter;                                                                                                 \
    } else {                                                                                                     \
      uint32_t counter = h.shared_counter.load(std::memory_order_relaxed);                                       \
      if ((counter & banjo::hot::kCountdownMask) == 0) [[unlikely]] {                                            \
        banjo::hot::Check(h, ctx, base, counter);                                                                \
        h.shared_counter.store(counter, std::memory_order_relaxed);                                              \
        return;                                                                                                  \
      }                                                                                                          \
      h.shared_counter.store(counter - 1, std::memory_order_relaxed);                                            \
    }                                                                                                            \
    banjo::hot::n_##ID::Native(ctx, base);                                                                       \
  }
