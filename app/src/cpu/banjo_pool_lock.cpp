// banjo-nx - startup crash "guest read of 0x24 on thread F8000048": a race in the game's resource
// pool, made reachable by the PC/Switch timing.
//
// THE GAME
//   Two global pools, [0x82FAC600] and [0x82FAC604], are created lazily and reset by:
//     sub_82364A20(which)  getter: if pool0 == 0, sub_82364A88(0x100000, 1); return pool[which]
//     sub_82364A88(size,n) reset: destroy pool0 (sub_82364BB8 frees every block with
//                          meInternalFree), store 0; destroy pool1, store 0; create both again.
//   The main thread resets them on display/movie transitions (sub_823C5F50, sub_823C6260,
//   sub_823CF5C0, sub_822BC520). The loader thread (F8000048: sub_82327560 ... sub_829A4500 ->
//   sub_822C9FF0) calls the getter. Nothing makes the two exclusive.
//
// WHAT HAPPENS (traced: tools in banjo-nx-cpu/app-diag/src/cpu/banjo_heap_diag.cpp)
//   main:   reset ENTER (pools 40B97000 40B99CD0) - destroys pool0 - [0x82FAC600] = 0 - destroying pool1...
//   loader: getter sees pool0 == 0 -> reset ENTER -> pool0 == 0, skip -> pool1 != 0 -> destroys pool1 too
//   Every block of pool1 is freed twice. The second meInternalFree cannot place the pointer
//   (RtlSizeHeap -> -1 on all heaps), its heap lookup sub_82240178 returns -1, it reads the heap
//   handle through the empty entry -1 (r31 = 0): "read of guest 0x00000024". Logged twice:
//   "ALREADY FREED (#224288 of 224290) by 82364BF0 thread 6 (main)" then the loader's free.
//   When the timing is a little different the race does not crash but leaks a pool (both threads
//   create one and the last store wins).
//
// THE FIX
//   The reset and the getter's check-and-create run under one host recursive mutex: the getter
//   waits for a reset in progress and then sees the new pool. Nothing else changes (same guest
//   code, same order). Lock order: this mutex -> the allocator's lock (taken inside the reset);
//   no caller of the getter holds a pool or allocator lock when it calls it.
//   Off with banjo_pool_lock = false (A/B).

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include <mutex>

REXCVAR_DEFINE_BOOL(banjo_pool_lock, true, "Banjo/Fixes",
                    "Serialize the game's resource-pool reset (sub_82364A88) and its lazy getter "
                    "(sub_82364A20): fixes the startup crash 'read of guest 0x24' (double free)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
std::recursive_mutex& PoolMutex() {
  static std::recursive_mutex m;
  return m;
}
}  // namespace

REX_EXTERN(__imp__sub_82364A88);
REX_HOOK_RAW(sub_82364A88) {
  if (!REXCVAR_GET(banjo_pool_lock)) {
    __imp__sub_82364A88(ctx, base);
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(PoolMutex());
  __imp__sub_82364A88(ctx, base);
}

REX_EXTERN(__imp__sub_82364A20);
REX_HOOK_RAW(sub_82364A20) {
  if (!REXCVAR_GET(banjo_pool_lock)) {
    __imp__sub_82364A20(ctx, base);
    return;
  }
  std::unique_lock<std::recursive_mutex> lock(PoolMutex(), std::try_to_lock);
  if (!lock.owns_lock()) {
    // Another thread is inside a pool reset: before this fix, this is where the getter could see
    // pool0 == 0 half-way through and destroy pool1 a second time.
    REXLOG_INFO("[banjo_pool_lock] pool getter (lr {:08X}) waited for a pool reset in progress",
                uint32_t(ctx.lr));
    lock.lock();
  }
  __imp__sub_82364A20(ctx, base);
}
