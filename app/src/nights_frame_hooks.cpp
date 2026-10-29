// nights-nx (2026-10-29): NiGHTS's frame hook on D3DDevice_Swap (sub_82346560: XDK 21173, paired with Peggle 2's
// sub_8261FE50 and Zuma's Revenge's sub_82309410, docs/nights-notes.md).
//
// - Counts game frames (g_banjo_game_frames: the captures' video and frame-rate lines read it; banjo-nx counted them in
//   its N&B-only ring renderer hooks, not built here).
// - With Renut's renderer, Renut's Swap presents (renut_native_sub_82346560, BANJO_RENUT_HOOK_SHARED in
//   renut_engine/gpu/d3d/hooks/device.cpp) and the frame is paced here (from peggle2-nx's PaceRenutSwap): Renut presents
//   without the console's vertical blank and its Swap does not wait for one, and NiGHTS's game speed follows its frame
//   rate. The console's Swap waits for the flip every 'interval' vblanks of 60 Hz (the device word at +14000, the
//   presentation interval: NiGHTS sets 2, 30 frames a second). A frame that is already late goes at once (no catching
//   up), as a late flip does.
// Credits: peggle2-nx's frame hook (the pacing), banjo-nx's display_sync (the release on the display's refresh).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#if BANJO_RENUT_RENDERER
#include "renut_engine/display_sync.h"
#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/banjo_shared_hooks.h"
#endif

std::atomic<uint64_t> g_banjo_game_frames{0};

// banjo-nx's controls overlay persists rebinds through its N&B cvar menu (renut_engine/cvar_menu.cpp, N&B-only, not
// built): here the rebinds last for the session.
void RenutSaveConfig() {
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("[nights] controls: rebinds are kept for this session only (no cvar menu in this build)");
  }
}

#if BANJO_RENUT_RENDERER
REXCVAR_DEFINE_BOOL(nights_renut_swap_pacing, true, "NiGHTS",
                    "Renut's renderer: the Swap waits for the presentation interval the game set (60 Hz vblanks), as "
                    "the console's does; false = frames as fast as they come (timing tests only: the game speeds up)");

namespace {

// The device's presentation interval (Peggle 2's layout, which XDK 21173 shares: device+14000).
constexpr uint32_t kDeviceOffsetPresentInterval = 14000;

void PaceRenutSwap(uint32_t interval) {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point previous{};
  // D3DPRESENT_INTERVAL_DEFAULT (0) waits for one vblank, as ONE does; IMMEDIATE (0x80000000) for none.
  if (interval & 0x80000000u) return;
  if (interval == 0) interval = 1;
  if (!REXCVAR_GET(nights_renut_swap_pacing)) return;
  const auto frame = std::chrono::nanoseconds(16'666'667LL * std::min<uint32_t>(interval, 4));
  const auto now = Clock::now();
  // nights-nx (2026-10-29, nights_renut_queue_drain, sa2-nx's): this release a refresh later, once, when the display
  // thread asks (double buffering's queue stuck one image deep: display_sync.h). The game's frame starts a refresh
  // later; its speed follows its frames, so it loses that one refresh, as at one slow frame.
  const bool hold = banjo::display_sync::TakeHold();
  constexpr uint64_t kRefreshNs = 16'666'667ull;
  // With the display's refresh known (the Switch: the display's vsync event), the release is a refresh plus the
  // measured phase (banjo-nx's banjo_display_sync); otherwise the timer below.
  banjo::display_sync::Start();
  {
    static uint64_t previous_release = 0;
    const auto to_ns = [](Clock::time_point t) {
      return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
    };
    bool anchored = false;
    uint64_t release = banjo::display_sync::NextRelease(to_ns(now), previous_release, uint64_t(frame.count()),
                                                        &anchored);
    // One refresh later: on the display's grid when the release was anchored to it, after 'now' when it was a late
    // frame's (at once).
    if (release && hold)
      release += kRefreshNs;
    if (release) {
      previous_release = release;
      const Clock::time_point at{std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(release))};
      for (;;) {
        const auto t = Clock::now();
        if (t >= at) break;
        if (at - t > std::chrono::microseconds(400))
          std::this_thread::sleep_for(at - t - std::chrono::microseconds(400));
        else
          std::this_thread::yield();
      }
      banjo::display_sync::NoteRelease(release, anchored);
      previous = std::max(at, now);
      return;
    }
    previous_release = 0;
  }
  auto due = previous + frame;
  if (hold && previous != Clock::time_point{})
    due = std::max(due, now) + std::chrono::nanoseconds(kRefreshNs);  // nights_renut_queue_drain: a refresh later
  if (previous == Clock::time_point{} || due <= now) {
    previous = now;
    return;
  }
  // The last part is yielded away instead of slept: Windows' sleeps overshoot by up to ~1 ms; Horizon's
  // svcSleepThread is precise, so the console spins only the last 0.4 ms.
#if REX_PLATFORM_SWITCH
  constexpr auto kTail = std::chrono::microseconds(400);
#else
  constexpr auto kTail = std::chrono::microseconds(1500);
#endif
  for (;;) {
    const auto t = Clock::now();
    if (t >= due) break;
    const auto left = due - t;
    if (left > kTail)
      std::this_thread::sleep_for(left - kTail);
    else
      std::this_thread::yield();
  }
  previous = due;
}

}  // namespace
#endif

#if BANJO_RENUT_RENDERER
namespace {
// The device's presentation interval as the game left it before a present (read before Renut's present runs).
uint32_t PresentInterval(const uint8_t *base, uint32_t device) {
  uint8_t b[4];
  std::memcpy(b, base + device + kDeviceOffsetPresentInterval, 4);
  const uint32_t interval = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
  static uint32_t last = ~0u;
  if (interval != last) {
    last = interval;
    REXLOG_INFO("[nights] present: presentation interval (device+14000) {:#x} at game frame {}", interval,
                g_banjo_game_frames.load(std::memory_order_relaxed));
  }
  return interval;
}
// After Renut presented a frame (from Swap or Present): count it and pace it.
void AfterRenutPresent(uint32_t interval) {
  g_banjo_game_frames.fetch_add(1, std::memory_order_relaxed);
  PaceRenutSwap(interval);
}
}  // namespace
#endif

// nights-nx (2026-10-29, the true 60 fps question): the engine's device set-up sub_825999B0 (a virtual method; r4 =
// its graphics settings) passes the settings' word at +28 as the presentation interval (2: 30 fps, 1: 60, other:
// immediate). nights_true60_interval > 0 writes that word before the set-up runs, so every reader of the setting sees
// it: an experiment (does the game step by it, or by frames?).
REXCVAR_DEFINE_INT32(nights_true60_interval, 0, "NiGHTS",
                     "Experiment: the engine's presentation-interval setting (its graphics settings' word +28) forced "
                     "to this value before the device is made (1 = 60 fps, 2 = the game's 30); 0 = as the game sets it")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REX_EXTERN(__imp__sub_825999B0);
REX_HOOK_RAW(sub_825999B0) {  // the engine's device set-up
  const int32_t forced = REXCVAR_GET(nights_true60_interval);
  uint32_t word;
  std::memcpy(&word, base + ctx.r4.u32 + 28, 4);
  word = __builtin_bswap32(word);
  REXLOG_INFO("[nights] device set-up: settings at {:08X}, presentation interval setting {}{}", ctx.r4.u32, word,
              forced > 0 ? fmt::format(" -> {} (nights_true60_interval)", forced) : std::string());
  if (forced > 0) {
    const uint32_t be = __builtin_bswap32(uint32_t(forced));
    std::memcpy(base + ctx.r4.u32 + 28, &be, 4);
  }
  __imp__sub_825999B0(ctx, base);
}

// nights-nx (2026-10-29, the true 60 fps survey): the frame body sub_82138B08 runs the task system twice (update, then
// draw); its update pass walks the task list at 0x82790158 (node: +0 next, +8 task; a task with a word at +44 or +52
// set is skipped) and calls each task's exec method (vtable + 8). nights_task_survey = N > 0 logs, every N game
// frames, how many tasks the list holds and their exec methods by count: the work a true 60 would have to go through.
REXCVAR_DEFINE_INT32(nights_task_survey, 0, "NiGHTS",
                     "Diagnostics: every N game frames, the task list's size and its tasks' exec methods (0 = off)")
    .range(0, 100000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace {
uint32_t GuestWord(const uint8_t *base, uint32_t a) {
  uint32_t w;
  std::memcpy(&w, base + a, 4);
  return __builtin_bswap32(w);
}
}  // namespace
REX_EXTERN(__imp__sub_82138B08);
REX_HOOK_RAW(sub_82138B08) {  // the frame body
  static const int32_t every = REXCVAR_GET(nights_task_survey);
  static uint64_t frames = 0;
  static std::unordered_map<uint32_t, uint32_t> seen;  // exec method -> tasks seen with it (all frames so far)
  if (every > 0) {
    std::unordered_map<uint32_t, uint32_t> now;
    uint32_t tasks = 0;
    for (uint32_t node = GuestWord(base, 0x82790158u); node && tasks < 100000; node = GuestWord(base, node)) {
      const uint32_t task = GuestWord(base, node + 8);
      ++tasks;
      if (!task || GuestWord(base, task + 44) || GuestWord(base, task + 52))
        continue;
      const uint32_t exec = GuestWord(base, GuestWord(base, task) + 8);
      ++now[exec];
      ++seen[exec];
    }
    if (++frames % uint64_t(every) == 0) {
      std::vector<std::pair<uint32_t, uint32_t>> list(now.begin(), now.end());
      std::sort(list.begin(), list.end(), [](auto &a, auto &b) { return a.second > b.second; });
      std::string top;
      for (size_t i = 0; i < list.size() && i < 40; ++i)
        top += fmt::format(" {:08X}x{}", list[i].first, list[i].second);
      REXLOG_INFO("[nights] task survey, game frame {}: {} tasks in the list, {} exec methods now, {} seen so far;{}",
                  frames, tasks, now.size(), seen.size(), top);
    }
  }
  __imp__sub_82138B08(ctx, base);
}

REX_EXTERN(__imp__sub_82346560);
REX_HOOK_RAW(sub_82346560) {  // D3DDevice_Swap
#if BANJO_RENUT_RENDERER
  if (::banjo::renut_gpu::Active()) {
    const uint32_t interval = PresentInterval(base, ctx.r3.u32);
    renut_native_sub_82346560(ctx, base);  // Renut's Swap (presents)
    AfterRenutPresent(interval);
    return;
  }
#endif
  __imp__sub_82346560(ctx, base);
  g_banjo_game_frames.fetch_add(1, std::memory_order_relaxed);
}

// D3DDevice_Present (sub_82346CC8, paired through Zuma's Revenge's sub_82309B78): NiGHTS presents this way, the back
// buffer resolved into the automatic front buffer and swapped (Renut's Present swaps host-side, without the guest
// Swap, so a frame is counted and paced once). Without Renut the original calls the guest Swap, which counts it.
REX_EXTERN(__imp__sub_82346CC8);
REX_HOOK_RAW(sub_82346CC8) {  // D3DDevice_Present
#if BANJO_RENUT_RENDERER
  if (::banjo::renut_gpu::Active()) {
    const uint32_t interval = PresentInterval(base, ctx.r3.u32);
    renut_native_sub_82346CC8(ctx, base);  // Renut's Present
    AfterRenutPresent(interval);
    return;
  }
#endif
  __imp__sub_82346CC8(ctx, base);
}
