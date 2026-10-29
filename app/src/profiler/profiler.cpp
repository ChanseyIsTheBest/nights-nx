// banjo-nx profiler core. See profiler.h.

#include "profiler/profiler.h"

#include <rex/cvar.h>
#include <rex/platform.h>

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

#if REX_PLATFORM_SWITCH
#include <switch.h>

#include "profiler/switch_cpu_profiler.h"
#include "profiler/switch_stall_watch.h"
#endif

REXCVAR_DEFINE_BOOL(banjo_show_profiler, false, "Nuts&Bolts/Profiler",
    "Show the CPU/GPU profiler window (Unleashed's F1 profiler; F5 toggles it on PC).");
// banjo-nx (2026-10-21, the user's request: off in every config): reNut's FPS counter in the top-left corner (F1 on PC).
REXCVAR_DEFINE_BOOL(banjo_show_fps, true, "Nuts&Bolts/Profiler",
    "Show reNut's FPS counter (frames per second, CPU and GPU ms) in the top-left corner (F1 toggles it on PC). Read "
    "at start.");
REXCVAR_DEFINE_BOOL(banjo_cpu_profiler, false, "Nuts&Bolts/Profiler",
    "[Switch] Sampling CPU profiler: pauses each thread every 2 ms to read where it is; every 30 s writes the "
    "hottest code of each thread to logs/rex/banjo_profiler.log (name it with tools/switch-cpu-profile.py).")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_slow_frame_profile_ms, 0, "Nuts&Bolts/Profiler",
    "[Switch] With banjo_cpu_profiler: frames whose game-thread work took at least this many ms get a report of "
    "their own (0 = off).")
    .range(0, 1000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(banjo_stall_watch_seconds, 1, "Nuts&Bolts/Profiler",
    "[Switch] Stall watchdog: after this many seconds without a game frame, write what every thread is doing to "
    "logs/rex/banjo_profiler.log, and count frames over 100 ms (0 = off).")
    .range(0, 60)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::profiler {
namespace {

constexpr size_t kSeries = static_cast<size_t>(Series::kCount);

struct Value {
  std::atomic<double> ms{0.0};
  std::atomic<bool> set{false};
};
std::array<Value, kSeries> g_values;

void Store(Series s, double ms) {
  auto& v = g_values[static_cast<size_t>(s)];
  v.ms.store(ms, std::memory_order_relaxed);
  v.set.store(true, std::memory_order_relaxed);
}

using Clock = std::chrono::steady_clock;
Clock::time_point g_lastFrame{};
Clock::time_point g_lastPresent{};
std::atomic<bool> g_mainRegistered{false};

std::mutex g_infoMutex;
std::string g_api = "ReXGlue Xenos emulation";
std::string g_device;

}  // namespace

const char* SeriesName(Series s) {
  switch (s) {
    case Series::kApplication: return "Application";
    case Series::kTick: return "Update (appMainTickPreDraw)";
    case Series::kDraw: return "Draw (appMainDraw)";
    case Series::kLimiterWait: return "Frame Limiter Wait";
    case Series::kGpuFrame: return "GPU Frame";
    case Series::kPresent: return "Present";
    case Series::kRenderThread: return "Render thread (Renut)";
    default: return "?";
  }
}

double Current(Series s) { return g_values[static_cast<size_t>(s)].ms.load(std::memory_order_relaxed); }
bool HasValue(Series s) { return g_values[static_cast<size_t>(s)].set.load(std::memory_order_relaxed); }

void RecordTick(double ms) { Store(Series::kTick, ms); }
void RecordDraw(double ms) { Store(Series::kDraw, ms); }
void RecordLimiterWait(double ms) { Store(Series::kLimiterWait, ms); }
void SetGpuFrameMs(double ms) { Store(Series::kGpuFrame, ms); }
void SetRenderThreadMs(double ms) { Store(Series::kRenderThread, ms); }

void RecordHostPresent() {
  // Called on one thread (the presenter's); no lock needed for the timestamp.
  const auto now = Clock::now();
  if (g_lastPresent != Clock::time_point{}) {
    Store(Series::kPresent, std::chrono::duration<double, std::milli>(now - g_lastPresent).count());
  }
  g_lastPresent = now;
}

void FrameBoundary() {
  const auto now = Clock::now();
  if (g_lastFrame != Clock::time_point{}) {
    Store(Series::kApplication, std::chrono::duration<double, std::milli>(now - g_lastFrame).count());
  }
  g_lastFrame = now;

#if REX_PLATFORM_SWITCH
  if (!g_mainRegistered.exchange(true, std::memory_order_relaxed)) {
    // The thread that runs the game loop; the slow-frame report only times its frames.
    os::switch_cpu_profiler::RegisterCurrentThread("main");
  }
  // Unleashed marks the end of a frame's work at the start of Present and the next frame's start at its
  // end; here both happen where the game hands its frame over.
  os::switch_cpu_profiler::FrameWorkEnd();
  os::switch_stall_watch::OnFrame();
  os::switch_cpu_profiler::FrameWorkStart();
#endif
}

void SetRendererInfo(const char* api, const char* device) {
  std::lock_guard<std::mutex> lock(g_infoMutex);
  g_api = api ? api : "";
  g_device = device ? device : "";
}

std::string RendererApi() {
  std::lock_guard<std::mutex> lock(g_infoMutex);
  return g_api;
}

std::string RendererDevice() {
  std::lock_guard<std::mutex> lock(g_infoMutex);
  return g_device;
}

std::string ThreadCpuUsage() {
  std::string out;
#if REX_PLATFORM_SWITCH
  os::switch_cpu_profiler::AppendThreadCpuUsage(out);
#endif
  return out;
}

void Start() {
#if REX_PLATFORM_SWITCH
  os::switch_cpu_profiler::SetSlowFrameThreshold(
      static_cast<uint32_t>(REXCVAR_GET(banjo_slow_frame_profile_ms)));
  os::switch_cpu_profiler::Start(REXCVAR_GET(banjo_cpu_profiler));
  os::switch_stall_watch::Start(static_cast<double>(REXCVAR_GET(banjo_stall_watch_seconds)));
#endif
}

}  // namespace banjo::profiler

#if REX_PLATFORM_SWITCH
// Thread hooks called by the SDK's libnx wrappers (sdk/src/ui/switch_perf.cpp). Every thread is known
// from its creation (as "thread") and renamed when it names itself through pthread_setname_np.
extern "C" {
void RexSwitchAppThreadCreated(u32 handle) {
  os::switch_cpu_profiler::RegisterThread(handle, "thread");
}
void RexSwitchAppThreadNamed(u32 handle, const char* name) {
  os::switch_cpu_profiler::RegisterThread(handle, name);
}
void RexSwitchAppThreadClosed(u32 handle) {
  os::switch_cpu_profiler::UnregisterThread(handle);
}
}
#endif
