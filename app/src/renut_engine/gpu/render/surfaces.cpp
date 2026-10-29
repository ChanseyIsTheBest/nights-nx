/**
 * @file    gpu/render/surfaces.cpp
 */
#include "renut_engine/gpu/render/surfaces.h"

#include <algorithm>
#include <atomic>
#include <chrono>  // banjo-nx (2026-10-28): dynamic resolution
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/render/format.h"
#include "renut_engine/gpu/render/graphics_options.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/guest/widescreen.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/video.h"
#if defined(PLUME_DEKO3D)
#include <plume_deko3d.h>  // banjo-nx (2026-10-21, from sa2-nx 1.0.3.1): Deko3DSetSwapChainSize
#endif
#if defined(__SWITCH__)
#include <switch.h>
#include <rex/ui/switch_saltynx.h>  // banjo-nx (2026-10-21): the console's mode, Reverse-NX obeyed
#endif

REXCVAR_DEFINE_STRING(renut_resolution, "720p", "Nuts&Bolts/Graphics",
                      "Internal render resolution, by its height: 480p, 720p (the game's own), "
                      "1080p, 1440p or 4K (2160 lines). Applies from the next frame.")
    .allowed({"480p", "720p", "1080p", "1440p", "4K"});

namespace renut::gpu {

namespace {

// ResolutionScale, once latched.
RenderScale g_resolution_scale;
bool g_resolution_latched = false;

// banjo-nx (2026-10-21, internal resolution): UiScale, the composite's UI target and this frame's redirect to it, and how
// many frames in a row have had no composite (kMenuFrames of them: menus or loading, drawn at the display's resolution
// whole; from the start too, so the boot logos and title are).
constexpr uint32_t kMenuFrames = 30;
RenderScale g_ui_scale;
// The internal heights (render_scale.h codes 4 and 6), as the shaders read them: handheld | docked << 16.
uint32_t g_internal_heights = 720u | 720u << 16;
// banjo-nx (2026-10-28, GPU block, banjo_renut_dynamic_resolution): each mode's heights, lowest first (night_gpu5: the
// range's ends and banjo_renut_dynamic_steps between them), how many (0: no range for that mode; set once by
// LatchResolutionScaleLocked), the one in use (an index into the mode's) and the mode it is for.
constexpr int kMaxDynamicSteps = 10;
int32_t g_dynamic_steps[2][kMaxDynamicSteps] = {};
int g_dynamic_count[2] = {0, 0};
std::atomic<int> g_dynamic_step{0};
std::atomic<bool> g_dynamic_docked{false};
// banjo-nx (2026-10-28, night_gpu5): the heights render_scale.h's settable codes 4 and 6 hold now ([0] code 4, [1] code
// 6; the shaders read them from InternalHeightsWord), and the frame each was last the render scale. A height other than
// 720 (code 0) and 1080 (code 2) takes the code that holds it, or else the other code than the one in use, whose targets
// go first (CodeForHeightLocked): a range's steps take turns on the two codes.
int32_t g_code_height[2] = {0, 0};
uint64_t g_code_used[2] = {0, 0};
uint64_t g_latches = 0;
struct UiTarget {
  HostSurface *scene = nullptr;  // the composite's target it was made for, and that target's generation then
  uint64_t scene_generation = 0;
  std::unique_ptr<HostSurface> surface;
  bool redirect = false;
};
// The composite's target's, and (made when a draw after the composite tests depth: the menus' 3D icons) the depth
// target's bound with it.
UiTarget g_ui, g_ui_depth;
bool g_composite_seen = false;
uint32_t g_frames_without_composite = kMenuFrames;

struct FramebufferKey {
  const HostSurface *colors[d3d::kMaxRenderTargets] = {};
  uint64_t color_generations[d3d::kMaxRenderTargets] = {};
  const HostSurface *depth = nullptr;
  uint64_t depth_generation = 0;

  bool operator==(const FramebufferKey &o) const {
    for (uint32_t i = 0; i < d3d::kMaxRenderTargets; ++i) {
      if (colors[i] != o.colors[i] || color_generations[i] != o.color_generations[i])
        return false;
    }
    return depth == o.depth && depth_generation == o.depth_generation;
  }
};

struct CachedFramebuffer {
  FramebufferKey key;
  std::unique_ptr<plume::RenderFramebuffer> framebuffer;
};

// A D3DSurface is a view of EDRAM: its base tile (RB_COLOR_INFO /
// RB_DEPTH_INFO bits 0-11), pitch (RB_SURFACE_INFO bits 0-13) and sample
// count. Surface objects over the same EDRAM with the same layout see the same
// pixels on the GPU, and N&B relies on it: the depth surface its after-effects
// pass binds is a second object over the scene's depth buffer (the character
// holograms test against it), and each hologram renders through surfaces of
// its own at the same EDRAM bases. So host targets belong to the EDRAM view,
// shared by every surface object over it, as long as they also agree on the
// host format and render scale: graphics_options renders some surfaces at
// another scale (the post-processing buffers at 1x), and a view used by
// surfaces of both kinds would otherwise be rebuilt at every switch.
uint64_t TargetKey(const d3d::SurfaceDesc &desc, bool depth, plume::RenderFormat format,
                   RenderScale scale) {
  const u32 info = desc.SurfaceInfo;
  return uint64_t(u32(desc.ColorInfo) & 0xFFF) | uint64_t(info & 0x3FFF) << 12 |
         uint64_t((info >> 16) & 3) << 26 | uint64_t(depth) << 28 |
         uint64_t(scale.code & 7) << 29 | uint64_t(u32(format)) << 32;
}

std::unordered_map<uint64_t, std::unique_ptr<HostSurface>> g_targets;
// Surface VA -> its target's key, and how many surfaces use each target.
std::unordered_map<u32, uint64_t> g_surface_keys;
std::unordered_map<uint64_t, uint32_t> g_target_users;
std::vector<CachedFramebuffer> g_framebuffers;
// banjo-nx (2026-10-19, render-thread proposal 8): the last framebuffers looked up, checked before the search of every
// framebuffer made (a linear search with an 80-byte compare at every draw's BindTargets); emptied whenever a cached
// framebuffer is retired (g_framebuffer_epoch).
struct RecentFramebuffer {
  FramebufferKey key;
  plume::RenderFramebuffer *framebuffer = nullptr;
  uint64_t epoch = 0;
};
RecentFramebuffer g_recent_framebuffers[4];
uint64_t g_framebuffer_epoch = 1;
uint64_t g_next_generation = 1;
// banjo-nx (2026-10-19, banjo_deko3d_depth24): the D3D depth formats each EDRAM depth view has been bound as (1 D24S8,
// 2 D24FS8). The host format is part of a target's key, so a view used as both would get two targets where the console
// has one memory: once a view has been bound as D24FS8, every surface over it takes the 32-bit float format.
std::unordered_map<uint64_t, uint8_t> g_depth_view_formats;
// SetTilingExtentLocked; 0 x 0 when not tiling.
uint32_t g_tiling_width = 0;
uint32_t g_tiling_height = 0;

// Drops cached framebuffers that reference 'surface'.
void ForgetFramebuffers(const HostSurface *surface) {
  for (size_t i = 0; i < g_framebuffers.size();) {
    const FramebufferKey &key = g_framebuffers[i].key;
    bool uses = key.depth == surface;
    for (const HostSurface *color : key.colors)
      uses = uses || color == surface;
    if (uses) {
      Video::RetireLocked(std::move(g_framebuffers[i].framebuffer));
      g_framebuffers[i] = std::move(g_framebuffers.back());
      g_framebuffers.pop_back();
      ++g_framebuffer_epoch;
    } else {
      ++i;
    }
  }
}

// Retires the target and everything made over it (deferred past in-flight
// frames).
// banjo-nx (2026-10-19, CPU proposal 8, banjo_renut_cpu_memos): GetHostSurfaceLocked's result per surface VA and the
// three descriptor words it reads, valid while nothing it reads has changed (g_surface_epoch: bumped by every target
// built, retired, unused or rebound, the resolution scale latched and the tiling extent set).
struct SurfaceMemo {
  u32 va = 0, size = 0, info = 0, format = 0;
  uint64_t epoch = 0;
  HostSurface *surface = nullptr;
};
SurfaceMemo g_surface_memo[64];  // banjo-nx (2026-10-19, render-thread proposal 8): 64, indexed by a hash of the VA
uint64_t g_surface_epoch = 1;

void FreeDepthViews(std::vector<DepthImage::View> &views) {
  for (DepthImage::View &v : views) {
    if (v.slot != UINT32_MAX && v.slot != kInvalidDescriptor)
      Video::FreeTextureSlotLocked(v.slot);
    Video::RetireLocked(std::move(v.view));
  }
  views.clear();
}

void RetireTarget(HostSurface &surface) {
  // banjo-nx (2026-10-21, internal resolution): the composite's target goes, and its UI target with it.
  for (UiTarget *u : {&g_ui, &g_ui_depth}) {
    if (&surface == u->scene) {
      u->scene = nullptr;
      u->redirect = false;
      if (u->surface && u->surface->texture)
        RetireTarget(*u->surface);
    }
  }
  if (surface.depth && surface.handed)
    DepthHandOverBeforeRetireLocked(&surface);  // banjo-nx (2026-10-19, banjo_deko3d_depth_handover)
  FreeDepthViews(surface.handover_views);
  surface.stencil_used = false;
  ++g_surface_epoch;
  ForgetFramebuffers(&surface);
  if (surface.sample_slot != UINT32_MAX)
    Video::FreeTextureSlotLocked(surface.sample_slot);
  surface.sample_slot = UINT32_MAX;
  Video::RetireLocked(std::move(surface.sample_view));
  if (surface.stencil_slot != UINT32_MAX && surface.stencil_slot != kInvalidDescriptor)
    Video::FreeTextureSlotLocked(surface.stencil_slot);
  surface.stencil_slot = UINT32_MAX;
  Video::RetireLocked(std::move(surface.stencil_view));
  Video::RetireLocked(std::move(surface.texture));
}

plume::RenderSampleCounts SampleCount(u32 info_word) {
  switch ((info_word >> 16) & 3) {
  case 1:
    return plume::RenderSampleCount::COUNT_2;
  case 2:
    return plume::RenderSampleCount::COUNT_4;
  default:
    return plume::RenderSampleCount::COUNT_1;
  }
}

// At least 'min_width' x 'min_height' (see SetTilingExtentLocked), at 'scale'.
bool BuildTarget(HostSurface &surface, const d3d::SurfaceDesc &desc, uint32_t min_width,
                 uint32_t min_height, RenderScale scale) {
  surface.size_word = desc.Size;
  surface.info_word = desc.SurfaceInfo;
  surface.format_word = desc.Format;
  surface.width = std::max(d3d::SurfaceWidth(surface.size_word), min_width);
  surface.height = std::max(d3d::SurfaceHeight(surface.size_word), min_height);
  surface.scale = scale;
  std::fill(std::begin(surface.profile_labels), std::end(surface.profile_labels), UINT32_MAX);
  const uint32_t host_width = surface.scale.Size(surface.width);
  const uint32_t host_height = surface.scale.Size(surface.height);
  surface.depth = d3d::IsDepthFormat(surface.format_word);
  surface.samples = SampleCount(surface.info_word);
  // banjo-nx (2026-10-21, banjo_renut_portrait_msaa_samples, NOT exact, the user's call): the small targets the game makes
  // with MSAA (the dialogue portrait's 96 x 96 box, 4x; colour and depth alike, so a framebuffer's attachments still
  // match) with fewer samples: 2 for 2x, 1 single-sampled. The pipelines take the target's sample count; single-sampled,
  // its resolves become copies.
  if (const uint32_t want = banjo::renut_gpu::PortraitMsaaSamples();
      want && uint32_t(surface.samples) > want && surface.width <= 128 && surface.height <= 128) {
    static uint32_t logged = 0;
    if (logged++ < 8)
      RGPU_INFO("Surface {:08X}: {}x{} {} with x{} MSAA made x{} (banjo_renut_portrait_msaa_samples)", surface.guest_va,
                surface.width, surface.height, surface.depth ? "depth" : "colour", u32(surface.samples), want);
    surface.samples = want == 2 ? plume::RenderSampleCount::COUNT_2 : plume::RenderSampleCount::COUNT_1;
  }
  if (surface.depth) {
    surface.format = DepthTargetFormat(surface.format_word);  // banjo-nx (2026-10-19, banjo_deko3d_depth24)
    if (banjo::renut_gpu::Depth24On()) {
      // As GetHostSurfaceSlow keys it: a view ever bound as D24FS8 is 32-bit float for every surface over it.
      const auto it = g_depth_view_formats.find(TargetKey(desc, true, plume::RenderFormat::UNKNOWN, scale));
      if (it != g_depth_view_formats.end() && (it->second & 2))
        surface.format = DepthTargetFormat();
    }
    if (surface.format != DepthTargetFormat()) {
      static uint32_t logged = 0;
      if (logged++ < 16)
        RGPU_INFO("Depth target {:08X}: {}x{} x{} samples, 24-bit unorm depth with stencil (banjo_deko3d_depth24)",
                  surface.guest_va, surface.width, surface.height, u32(surface.samples));
    }
  } else {
    surface.format = SurfaceColorFormat(desc);
  }

  plume::RenderDevice *device = Video::Device();
  const plume::RenderMultisampling multisampling(surface.samples);
  // Sampled too: resolves read them, and so does scan-out of a bound target.
  if (surface.depth) {
    surface.texture = device->createTexture(plume::RenderTextureDesc::DepthTarget(
        host_width, host_height, surface.format, multisampling));
  } else {
    surface.texture = device->createTexture(plume::RenderTextureDesc::ColorTarget(
        host_width, host_height, surface.format, multisampling));
  }
  surface.layout = plume::RenderTextureLayout::UNKNOWN;
  surface.generation = g_next_generation++;
  if (!TextureCreated(surface.texture.get())) {
    surface.texture.reset();
    if (!ReportDeviceLost(device)) {
      RGPU_ERROR("Surface {:08X}: {}x{} {} target creation failed", surface.guest_va,
                 host_width, host_height, surface.depth ? "depth" : "color");
    }
    return false;
  }
  surface.texture->setName(surface.depth ? "renut depth surface" : "renut color surface");
  RGPU_DEBUG("Surface {:08X}: {}x{} (x{:.3g} scale) {} format {} x{} samples", surface.guest_va,
             surface.width, surface.height, surface.scale.Factor(), surface.depth ? "depth" : "color",
             u32(surface.format), u32(surface.samples));
  return true;
}

#if defined(PLUME_DEKO3D)
// banjo-nx (2026-10-28, night_gpu5, dynamic resolution steps): the scale of 'height' lines. 720 is code 0 and 1080 code
// 2 (fixed factors); any other height takes the settable code (4 or 6) that holds it, or else the one that is not the
// render scale now (the less recently used when neither is), set to the height here. That code's targets are retired
// first, their size being the old height's: each is built again at the new one at its next use (GetHostSurfaceSlow), as
// a target that grows is. Resolve destinations of the code are made again at their next resolve (textures.cpp, by
// their size). Between two such changes of a code the frame has drawn at the other one, so a resolve texture still of
// the old height and sampled after it has gone two changes without a resolve into it.
RenderScale CodeForHeightLocked(int32_t height) {
  if (height == 720)
    return RenderScale{};
  if (height == 1080)
    return RenderScale{2};
  constexpr uint32_t kCodes[2] = {RenderScale::kHandheldCode, RenderScale::kDockedCode};
  for (int i = 0; i < 2; ++i) {
    if (g_code_height[i] == height) {
      g_code_used[i] = g_latches;
      return RenderScale{kCodes[i]};
    }
  }
  int pick;
  if (g_resolution_scale.code == kCodes[0])
    pick = 1;
  else if (g_resolution_scale.code == kCodes[1])
    pick = 0;
  else
    pick = g_code_used[0] <= g_code_used[1] ? 0 : 1;
  const uint32_t code = kCodes[pick];
  uint32_t retired = 0;
  for (auto &[key, surface] : g_targets) {
    if (surface && surface->texture && surface->scale.code == code) {
      RetireTarget(*surface);
      ++retired;
    }
  }
  const uint32_t g = std::gcd(uint32_t(height), 720u);
  RenderScale::kNumerators[code] = uint32_t(height) / g;
  RenderScale::kDenominators[code] = 720u / g;
  const int32_t old_height = g_code_height[pick];
  g_code_height[pick] = height;
  g_code_used[pick] = g_latches;
  g_internal_heights = pick == 0 ? (g_internal_heights & 0xFFFF0000u) | uint32_t(height)
                                 : (g_internal_heights & 0xFFFFu) | uint32_t(height) << 16;
  ++g_surface_epoch;
  RGPU_INFO("[renut] dynamic resolution: code {} now {} lines ({} targets of its {} lines made again at their next use)",
            code, height, retired, old_height);
  return RenderScale{code};
}
#endif

} // namespace

uint64_t SurfaceEpochLocked() { return g_surface_epoch; }  // banjo-nx (2026-10-20, from sa2-nx, banjo_renut_chain_reuse)
uint64_t FramebufferEpochLocked() { return g_framebuffer_epoch; }  // banjo-nx (2026-10-20, from sa2-nx, banjo_renut_chain_reuse)

RenderScale ResolutionScale() {
  return g_resolution_scale;
}

void LatchResolutionScaleLocked() {
  LatchGraphicsOptionsLocked();
  const std::string resolution = REXCVAR_GET(renut_resolution);
  // Codes of RenderScale.
  RenderScale scale{resolution == "480p"    ? 1u
                    : resolution == "1080p" ? 2u
                    : resolution == "1440p" ? 3u
                    : resolution == "4K"    ? 4u
                                            : 0u};
  RenderScale ui = scale;
  // banjo-nx (2026-10-18): the deko3d renderer drew at the console's resolution only (tools/deko3d_glsl_opt.py, pass
  // scale1). banjo-nx (2026-10-21, internal resolution, the user's request; the docked 1920x1080 swapchain from sa2-nx
  // 1.0.3.1): its package is made without scale1 and fragscale now (with scalecodes: codes 4 and 6 are 9/10 and 5/4,
  // render_scale.h), and the console's mode picks the heights: the display's (720 handheld, banjo_renut_display_height_
  // docked docked) for the HUD and the swapchain, banjo_renut_render_height_handheld / _docked for the 3D image of
  // gameplay frames (those with the final composite within the last kMenuFrames frames). renut_resolution is not taken.
  if (IsDeko3D()) {
    const bool docked = ConsoleDocked();
    const int32_t display = docked ? banjo::renut_gpu::DisplayHeightDocked() : 720;
    const int32_t render =
        docked ? banjo::renut_gpu::RenderHeightDocked() : banjo::renut_gpu::RenderHeightHandheld();
    RenderScale display_scale, render_scale;
    if (!RenderScale::FromRatio(uint32_t(std::max(display, 1)), 720, display_scale) ||
        (display_scale.code != 0 && display_scale.code != 2)) {
      static bool logged = false;
      if (!logged) {
        logged = true;
        RGPU_WARN("banjo_renut_display_height_docked = {} is not 720 or 1080: 720", display);
      }
      display_scale = {};
    }
    // banjo-nx (2026-10-21, package 36z2, the user's request): any height of 360 to 1080 for either mode, read once
    // (the keys are the config's): codes 4 (handheld) and 6 (docked) of render_scale.h get height / 720, rounded to a
    // multiple of 9 so every 16:9 size the game uses (1280x720, 640x360, 320x180) is whole host pixels at it, and the
    // shaders the same heights. 720 is 1x (code 0); 1080 is code 2 (1.5x, the docked display's: no upscale then).
    static const struct Heights {
      int32_t handheld, docked;
      bool dynamic[2];  // banjo-nx (2026-10-28): banjo_renut_dynamic_resolution's range for [handheld, docked]
    } heights = [] {
      auto pick = [](int32_t want, const char *name) {
        const int32_t h = std::clamp((std::clamp(want, 360, 1080) + 4) / 9 * 9, 360, 1080);
        if (h != want)
          RGPU_INFO("banjo_renut_render_height_{} = {}: {} (a multiple of 9 from 360 to 1080)", name, want, h);
        return h;
      };
      int32_t hh = pick(banjo::renut_gpu::RenderHeightHandheld(), "handheld");
      int32_t dh = pick(banjo::renut_gpu::RenderHeightDocked(), "docked");
      // banjo-nx (2026-10-28, GPU block, the user's ranges): each mode's range has 720 (1x, code 0) at one end; the
      // other end takes the mode's code (4 handheld, 6 docked) for the session. 2026-10-28 (night_gpu5, the user's
      // request: docked 720 to 1080): an end of 1080 is code 2 (1.5x, the docked display's own size: the HUD is drawn
      // into the scene without the upscale), as a fixed height of 1080 always was (render_scale below).
      bool dynamic[2] = {false, false};
      if (DynamicResolutionOn()) {
        for (int m = 0; m < 2; ++m) {
          const bool docked_mode = m == 1;
          const int32_t low = pick(DynamicHeight(docked_mode, false), docked_mode ? "dynamic docked low" : "dynamic handheld low");
          const int32_t high = pick(DynamicHeight(docked_mode, true), docked_mode ? "dynamic docked high" : "dynamic handheld high");
          if (low >= high || (low != 720 && high != 720)) {
            RGPU_WARN("[renut] dynamic resolution, {} mode: {} to {} lines is not a range with 720 at one end: the "
                      "fixed height", docked_mode ? "docked" : "handheld", low, high);
            continue;
          }
          (docked_mode ? dh : hh) = low == 720 ? high : low;
          dynamic[m] = true;
          // banjo-nx (2026-10-28, night_gpu5, banjo_renut_dynamic_steps): the ends and the heights between them, evenly
          // spaced and rounded to a multiple of 36 (whole host pixels at 320x180 too). Deko3d only: the other back ends'
          // codes 4 and 6 are fixed factors (the ends alone there).
          int n = 0;
          g_dynamic_steps[m][n++] = low;
#if defined(PLUME_DEKO3D)
          const int between = DynamicSteps();
          for (int i = 1; i <= between && n < kMaxDynamicSteps - 1; ++i) {
            const int32_t h = (low + (high - low) * i / (between + 1) + 18) / 36 * 36;
            if (h > g_dynamic_steps[m][n - 1] && h < high)
              g_dynamic_steps[m][n++] = h;
          }
#endif
          g_dynamic_steps[m][n++] = high;
          g_dynamic_count[m] = n;
          std::string list;
          for (int i = 0; i < n; ++i)
            list += fmt::format("{}{}", i ? " " : "", g_dynamic_steps[m][i]);
          RGPU_INFO("[renut] dynamic resolution, {} mode: 3D at {} lines, by the GPU's time a frame against {} ms "
                    "(banjo_renut_dynamic_resolution){}",
                    docked_mode ? "docked" : "handheld", list, DynamicGpuMs(),
                    DynamicTestSeconds() > 0 ? fmt::format("; test: a step every {} s", DynamicTestSeconds())
                                             : std::string());
        }
      }
#if defined(PLUME_DEKO3D)  // nights-nx: the table is constant on the PC's back ends (render_scale.h)
      auto set = [](uint32_t code, int32_t h) {
        const uint32_t g = std::gcd(uint32_t(h), 720u);
        RenderScale::kNumerators[code] = uint32_t(h) / g;
        RenderScale::kDenominators[code] = 720u / g;
      };
      set(RenderScale::kHandheldCode, hh);
      set(RenderScale::kDockedCode, dh);
      g_code_height[0] = hh;  // banjo-nx (2026-10-28, night_gpu5): CodeForHeightLocked's
      g_code_height[1] = dh;
#endif
      g_internal_heights = uint32_t(hh) | uint32_t(dh) << 16;
      RGPU_INFO("Internal heights: handheld {}x{}, docked {}x{}", RenderScale{RenderScale::kHandheldCode}.Size(1280),
                hh, RenderScale{RenderScale::kDockedCode}.Size(1280), dh);
      return Heights{hh, dh, {dynamic[0], dynamic[1]}};
    }();
    // banjo-nx (2026-10-28, GPU block, banjo_renut_dynamic_resolution): the mode's range by the GPU's time; a change of
    // mode starts it at its 720 end (night_gpu2 console run: docking at 900 made the docked set of targets with the heap
    // at its end; 720 is the smaller set, at the 1x code both modes use).
    const int mode = docked ? 1 : 0;
    const int count = heights.dynamic[mode] ? g_dynamic_count[mode] : 0;
    static bool dynamic_started = false;
    const bool mode_changed = g_dynamic_docked.exchange(docked) != docked;
    if ((mode_changed || !dynamic_started) && count) {
      dynamic_started = true;
      g_dynamic_step = g_dynamic_steps[mode][0] == 720 ? 0 : count - 1;  // the 720 end
    }
    ++g_latches;
    const int32_t chosen = count ? g_dynamic_steps[mode][std::clamp(g_dynamic_step.load(), 0, count - 1)]
                                 : (docked ? heights.docked : heights.handheld);
#if defined(PLUME_DEKO3D)
    render_scale = CodeForHeightLocked(chosen);  // banjo-nx (2026-10-28, night_gpu5): the steps take turns on codes 4 and 6
#else
    render_scale = chosen == 720    ? RenderScale{}
                   : chosen == 1080 ? RenderScale{2}
                                    : RenderScale{docked ? RenderScale::kDockedCode : RenderScale::kHandheldCode};
#endif
    (void)render;
    g_frames_without_composite = g_composite_seen ? 0 : std::min(g_frames_without_composite + 1, kMenuFrames);
    g_composite_seen = false;
    ui = display_scale;
    scale = g_frames_without_composite < kMenuFrames ? render_scale : display_scale;
#if defined(PLUME_DEKO3D)
    // The swapchain at the display's size (from sa2-nx 1.0.3.1): the present scales the frame to it.
    static int32_t last_display = 0;
    if (last_display != display) {
      last_display = display;
      plume::Deko3DSetSwapChainSize(display == 1080 ? 1920u : 1280u, display == 1080 ? 1080u : 720u);
      RGPU_INFO("[renut] {} mode: the swapchain at {}", docked ? "docked" : "handheld",
                display == 1080 ? "1920x1080" : "1280x720");
    }
#endif
  }
  // The frame's redirect to the UI target ends with it.
  for (UiTarget *u : {&g_ui, &g_ui_depth}) {
    if (u->redirect) {
      u->redirect = false;
      ++g_surface_epoch;
    }
  }
  if (ui != g_ui_scale) {
    g_ui_scale = ui;
    ++g_surface_epoch;
    RGPU_INFO("HUD resolution {}x{} ({:.3g}x)", ui.Size(GuestScreenWidth()), ui.Size(720), ui.Factor());
  }
  if (!g_resolution_latched || scale != g_resolution_scale) {
    RGPU_INFO("Render resolution {}x{} ({:.3g}x)", scale.Size(GuestScreenWidth()), scale.Size(720),
              scale.Factor());
    g_resolution_scale = scale;
    ++g_surface_epoch;
    g_resolution_latched = true;
  }
}

// banjo-nx (2026-10-28, GPU block): see render/surfaces.h. A moving average of the GPU's time a frame (about 16
// frames); down above the budget, up when the next height's predicted time (the current one times the pixel ratio, an
// overestimate) has been under 85 % of it for banjo_renut_dynamic_up_s in a row (and that long since the last change);
// down 5 s at least after a change. night_gpu5 (the user's request, banjo_renut_dynamic_steps): with heights between the
// ends, down goes to the highest step predicted to fit the budget (the lowest if none) and up to the highest predicted
// under 85 % of it, in one change each.
void NoteGpuFrameForDynamicResolution(double gpu_ms) {
  const int mode = g_dynamic_docked ? 1 : 0;
  const int count = g_dynamic_count[mode];
  if (count < 2 || gpu_ms <= 0.0)
    return;
  // banjo-nx (2026-10-28): up after banjo_renut_dynamic_up_s in a row of fitting, 1 s by default (the user: "be
  // aggressive"). h9 had 20 s, after a loading screen's light frames took the city's 540 to 720 for 12 s (each change
  // makes a set of targets); the h10 GPU region is what keeps that memory in check now.
  const double up_seconds = DynamicUpSeconds();
  static double average = 0.0;
  static std::chrono::steady_clock::time_point last_change = std::chrono::steady_clock::now();
  static std::chrono::steady_clock::time_point fits_since = last_change;
  static int last_mode = mode;
  average = average > 0.0 ? average * 0.94 + gpu_ms * 0.06 : gpu_ms;
  const auto now = std::chrono::steady_clock::now();
  if (mode != last_mode) {  // a change of mode starts again at its 720 end (see above): its own hold
    last_mode = mode;
    fits_since = now;
  }
  const double since = std::chrono::duration<double>(now - last_change).count();
  const int step = std::clamp(g_dynamic_step.load(), 0, count - 1);
  const int32_t *heights = g_dynamic_steps[mode];
  const double current = double(heights[step]);
  const double budget = DynamicGpuMs();
  // The time a step is predicted to take: the average times the pixel ratio.
  const auto predicted = [&](int s) {
    const double r = double(heights[s]) / current;
    return average * r * r;
  };
  int next = step;
  const int32_t test = DynamicTestSeconds();
  if (test > 0) {
    // The test walks the steps one at a time, up to an end and back.
    static int direction = -1;
    if (since >= double(test)) {
      if (step + direction < 0 || step + direction >= count)
        direction = -direction;
      next = step + direction;
    }
  } else if (step > 0 && since >= 5.0 && average > budget) {
    next = 0;
    for (int s = step - 1; s >= 0; --s) {
      if (predicted(s) <= budget) {
        next = s;
        break;
      }
    }
  } else if (step + 1 < count) {
    if (predicted(step + 1) >= budget * 0.85)
      fits_since = now;
    if (since >= up_seconds && std::chrono::duration<double>(now - fits_since).count() >= up_seconds) {
      next = step + 1;
      while (next + 1 < count && predicted(next + 1) < budget * 0.85)
        ++next;
    }
  }
  if (next == step)
    return;
  const double was = average;
  average = predicted(next);  // the frames to come are at the new height
  g_dynamic_step = next;
  last_change = now;
  fits_since = now;
  RGPU_INFO("[renut] dynamic resolution: 3D at {} lines (the GPU's time a frame {:.1f} ms on average at {}; "
            "banjo_renut_dynamic_gpu_ms {})",
            heights[next], was, heights[step], DynamicGpuMs());
}

void SetTilingExtentLocked(uint32_t width, uint32_t height) {
  if (g_tiling_width != width || g_tiling_height != height)
    ++g_surface_epoch;
  g_tiling_width = width;
  g_tiling_height = height;
}

// A surface stops using its target (destroyed, or its header now describes
// another view); the last user retires it.
void UnuseTarget(uint64_t key) {
  auto users = g_target_users.find(key);
  if (users == g_target_users.end() || --users->second)
    return;
  g_target_users.erase(users);
  ++g_surface_epoch;
  if (auto it = g_targets.find(key); it != g_targets.end()) {
    RetireTarget(*it->second);
    g_targets.erase(it);
  }
}

HostSurface *FindHostSurfaceLocked(u32 surface_va) {
  auto key = g_surface_keys.find(surface_va);
  if (key == g_surface_keys.end())
    return nullptr;
  auto it = g_targets.find(key->second);
  return it != g_targets.end() ? it->second.get() : nullptr;
}

void ColorAliasesLocked(const HostSurface *surface, std::vector<HostSurface *> &out) {
  out.clear();
  if (!surface || surface->depth)
    return;
  // TargetKey: the view in the low 32 bits, the host format above.
  constexpr uint64_t kViewMask = 0xFFFFFFFFull;
  uint64_t own = 0;
  bool found = false;
  for (const auto &[key, target] : g_targets) {
    if (target.get() == surface) {
      own = key;
      found = true;
      break;
    }
  }
  if (!found)
    return;
  for (const auto &[key, target] : g_targets) {
    if (target && target.get() != surface && target->texture && !target->depth &&
        (key & kViewMask) == (own & kViewMask))
      out.push_back(target.get());
  }
}

HostSurface *GetHostSurfaceSlow(u32 surface_va, const d3d::SurfaceDesc *desc);

HostSurface *GetHostSurfaceLocked(u32 surface_va) {
  if (!surface_va)
    return nullptr;
  // Targets made before the first Swap are of the configured scale too.
  if (!g_resolution_latched)
    LatchResolutionScaleLocked();
  const auto *desc = GuestPtr<const d3d::SurfaceDesc>(surface_va + sizeof(d3d::Resource));
  if (!desc)
    return nullptr;

  const bool memo = banjo::renut_gpu::CpuMemosOn();
  SurfaceMemo &m = g_surface_memo[(surface_va * 0x9E3779B1u) >> 26];
  if (memo && m.epoch == g_surface_epoch && m.va == surface_va && m.size == desc->Size &&
      m.info == desc->SurfaceInfo && m.format == desc->Format) {
    // the same as the lookup's: the surface bound now
    m.surface->size_word = desc->Size;
    m.surface->info_word = desc->SurfaceInfo;
    m.surface->format_word = desc->Format;
    // banjo-nx (2026-10-21, internal resolution): the composite's target after the composite: its UI target.
    if (g_ui.redirect && m.surface == g_ui.scene)
      return g_ui.surface.get();
    if (g_ui_depth.redirect && m.surface == g_ui_depth.scene)
      return g_ui_depth.surface.get();
    return m.surface;
  }
  HostSurface *result = GetHostSurfaceSlow(surface_va, desc);
  if (memo && result) {
    m.va = surface_va;
    m.size = desc->Size;
    m.info = desc->SurfaceInfo;
    m.format = desc->Format;
    m.epoch = g_surface_epoch;
    m.surface = result;
  }
  if (g_ui.redirect && result && result == g_ui.scene)
    return g_ui.surface.get();  // banjo-nx (2026-10-21, internal resolution)
  if (g_ui_depth.redirect && result && result == g_ui_depth.scene)
    return g_ui_depth.surface.get();
  return result;
}

// banjo-nx (2026-10-21, internal resolution): see surfaces.h.
RenderScale UiScale() { return g_ui_scale; }

bool UiRedirectedLocked() { return g_ui.redirect; }

void NoteCompositeLocked() { g_composite_seen = true; }

void RedirectToUiSurfaceLocked() {
  if (g_ui.scene && g_ui.surface && g_ui.surface->texture && !g_ui.redirect) {
    g_ui.redirect = true;
    ++g_surface_epoch;
  }
}

void RedirectDepthToUiLocked() {
  if (g_ui_depth.scene && g_ui_depth.surface && g_ui_depth.surface->texture && !g_ui_depth.redirect) {
    g_ui_depth.redirect = true;
    ++g_surface_epoch;
  }
}

uint32_t InternalHeightsWord() { return g_internal_heights; }

bool ConsoleDocked() {
#if defined(__SWITCH__)
  return rex::ui::switch_saltynx::BaseMode(appletGetOperationMode() == AppletOperationMode_Console);
#else
  return false;
#endif
}

namespace {

// The UI target 'u' for 'scene' (colour or depth): kept while it fits, made again otherwise.
HostSurface *MakeUiTarget(UiTarget &u, HostSurface *scene) {
  if (!scene || !scene->texture)
    return nullptr;
  HostSurface *ui = u.surface.get();
  if (ui && ui->texture && u.scene == scene && u.scene_generation == scene->generation &&
      ui->scale == g_ui_scale && ui->width == scene->width && ui->height == scene->height &&
      ui->format == scene->format && ui->samples == scene->samples) {
    ui->size_word = scene->size_word;
    ui->info_word = scene->info_word;
    ui->format_word = scene->format_word;
    return ui;
  }
  if (!ui) {
    u.surface = std::make_unique<HostSurface>();
    ui = u.surface.get();
  } else if (ui->texture) {
    RetireTarget(*ui);
  }
  ui->guest_va = scene->guest_va;
  ui->size_word = scene->size_word;
  ui->info_word = scene->info_word;
  ui->format_word = scene->format_word;
  ui->width = scene->width;
  ui->height = scene->height;
  ui->scale = g_ui_scale;
  ui->depth = scene->depth;
  ui->format = scene->format;
  ui->samples = scene->samples;
  ui->writes = 0;
  std::fill(std::begin(ui->profile_labels), std::end(ui->profile_labels), UINT32_MAX);
  const uint32_t host_width = ui->scale.Size(ui->width), host_height = ui->scale.Size(ui->height);
  plume::RenderDevice *device = Video::Device();
  const plume::RenderMultisampling multisampling(ui->samples);
  ui->texture = device->createTexture(
      ui->depth ? plume::RenderTextureDesc::DepthTarget(host_width, host_height, ui->format, multisampling)
                : plume::RenderTextureDesc::ColorTarget(host_width, host_height, ui->format, multisampling));
  ui->layout = plume::RenderTextureLayout::UNKNOWN;
  ui->generation = g_next_generation++;
  ++g_surface_epoch;
  if (!TextureCreated(ui->texture.get())) {
    ui->texture.reset();
    u.scene = nullptr;
    if (!ReportDeviceLost(device))
      RGPU_ERROR("UI {} target for {:08X}: {}x{} creation failed", ui->depth ? "depth" : "colour", scene->guest_va,
                 host_width, host_height);
    return nullptr;
  }
  ui->texture->setName(ui->depth ? "renut UI depth surface" : "renut UI surface");
  u.scene = scene;
  u.scene_generation = scene->generation;
  RGPU_INFO("UI {} target for {:08X}: {}x{} (the 3D image {}x{})", ui->depth ? "depth" : "colour", scene->guest_va,
            host_width, host_height, scene->scale.Size(scene->width), scene->scale.Size(scene->height));
  return ui;
}

}  // namespace

HostSurface *UiSurfaceLocked(HostSurface *scene) {
  return scene && !scene->depth ? MakeUiTarget(g_ui, scene) : nullptr;
}

HostSurface *UiDepthSurfaceLocked(HostSurface *scene_depth) {
  return scene_depth && scene_depth->depth ? MakeUiTarget(g_ui_depth, scene_depth) : nullptr;
}

HostSurface *GetHostSurfaceSlow(u32 surface_va, const d3d::SurfaceDesc *desc) {
  const bool depth = d3d::IsDepthFormat(desc->Format);
  plume::RenderFormat format = depth ? DepthTargetFormat(desc->Format) : SurfaceColorFormat(*desc);
  // The render resolution, or the finer one graphics_options gives it.
  const RenderScale scale = SurfaceRenderScale(*desc, g_resolution_scale);
  if (depth && banjo::renut_gpu::Depth24On()) {
    // banjo-nx (2026-10-19, banjo_deko3d_depth24): one view, one format (g_depth_view_formats).
    uint8_t &seen = g_depth_view_formats[TargetKey(*desc, true, plume::RenderFormat::UNKNOWN, scale)];
    const uint8_t bit = (desc->Format & 0x3F) == 23 ? 2 : 1;
    if ((seen | bit) == 3 && seen != 3) {
      static uint32_t logged = 0;
      if (logged++ < 8)
        RGPU_WARN("Depth view of surface {:08X} bound as both D24S8 and D24FS8: 32-bit float from now on "
                  "(banjo_deko3d_depth24)", surface_va);
    }
    seen |= bit;
    if (seen & 2)
      format = DepthTargetFormat();
  }
  const uint64_t key = TargetKey(*desc, depth, format, scale);
  auto [bound, first_use] = g_surface_keys.try_emplace(surface_va, key);
  if (first_use) {
    ++g_target_users[key];
    ++g_surface_epoch;
  } else if (bound->second != key) {
    ++g_surface_epoch;
    // The header was rewritten (or the VA reused without Destroy reaching
    // us): the surface is a view of other EDRAM now.
    UnuseTarget(bound->second);
    bound->second = key;
    ++g_target_users[key];
  }

  auto &slot = g_targets[key];
  // The view's target covers every surface over it, and every tile while
  // tiling (SetTilingExtentLocked).
  uint32_t min_width = std::max(g_tiling_width, d3d::SurfaceWidth(desc->Size));
  uint32_t min_height = std::max(g_tiling_height, d3d::SurfaceHeight(desc->Size));
  if (slot && slot->texture) {
    // The surface bound now: its guest size (Targets::guest_width) and D3D
    // format (D24S8 or D24FS8 for a depth resolve).
    slot->size_word = desc->Size;
    slot->info_word = desc->SurfaceInfo;
    slot->format_word = desc->Format;
    if (slot->width >= min_width && slot->height >= min_height && slot->scale == scale)
      return slot.get();
    // A larger surface over the view, or a larger tiling: grow it, keeping
    // the size the others gave it (what it held is lost). (Another scale is
    // another target: TargetKey.)
    min_width = std::max(min_width, slot->width);
    min_height = std::max(min_height, slot->height);
    RetireTarget(*slot);
  } else if (!slot) {
    slot = std::make_unique<HostSurface>();
    slot->guest_va = surface_va;
  }
  ++g_surface_epoch;
  if (!BuildTarget(*slot, *desc, min_width, min_height, scale))
    return nullptr;
  // No defined content yet; D3D12 wants a new target discarded (or cleared)
  // before anything else uses it, and a resolve may read it before any draw.
  if (plume::RenderCommandList *cmd = Video::CommandListLocked()) {
    TransitionSurfaceLocked(slot.get(), slot->depth ? plume::RenderTextureLayout::DEPTH_WRITE
                                                    : plume::RenderTextureLayout::COLOR_WRITE);
    cmd->discardTexture(slot->texture.get());
  }
  return slot.get();
}

HostSurface *SurfaceByGenerationLocked(uint64_t generation) {
  for (const auto &[key, surface] : g_targets)
    if (surface && surface->texture && surface->generation == generation)
      return surface.get();
  for (const UiTarget *u : {&g_ui, &g_ui_depth})  // banjo-nx (2026-10-21): the UI targets are no view's
    if (u->surface && u->surface->texture && u->surface->generation == generation)
      return u->surface.get();
  return nullptr;
}

void SurfaceMemoryLocked(size_t &targets, uint64_t &bytes) {
  targets = 0;
  bytes = 0;
  for (const auto &[key, surface] : g_targets) {
    if (!surface || !surface->texture)
      continue;
    ++targets;
    const uint64_t texel = plume::RenderFormatSize(surface->format);
    bytes += uint64_t(surface->scale.Size(surface->width)) * surface->scale.Size(surface->height) *
             texel * surface->samples;
  }
  for (const UiTarget *u : {&g_ui, &g_ui_depth}) {  // banjo-nx (2026-10-21)
    if (const HostSurface *ui = u->surface.get(); ui && ui->texture) {
      ++targets;
      bytes += uint64_t(ui->scale.Size(ui->width)) * ui->scale.Size(ui->height) *
               plume::RenderFormatSize(ui->format) * ui->samples;
    }
  }
}

void ReleaseHostSurfaceLocked(u32 surface_va) {
  auto it = g_surface_keys.find(surface_va);
  if (it == g_surface_keys.end())
    return;
  const uint64_t key = it->second;
  g_surface_keys.erase(it);
  ++g_surface_epoch;
  UnuseTarget(key);
}

uint32_t SurfaceSampleSlotLocked(HostSurface *surface) {
  if (!surface || !surface->texture)
    return kInvalidDescriptor;
  if (surface->sample_slot != UINT32_MAX)
    return surface->sample_slot;
  // The depth format views the depth plane.
  surface->sample_view = surface->texture->createTextureView(
      plume::RenderTextureViewDesc::Texture2D(surface->format));
  if (!surface->sample_view)
    return kInvalidDescriptor;
  const uint32_t slot = Video::AllocateTextureSlotLocked();
  if (slot == kInvalidDescriptor)
    return kInvalidDescriptor;
  Video::SetTextureSlotLocked(slot, surface->texture.get(), surface->sample_view.get());
  surface->sample_slot = slot;
  return slot;
}

// banjo-nx (2026-10-18): the bindless slot of a depth/stencil target's stencil view (read as uint by the depth
// resolve), or kInvalidDescriptor (no stencil, D3D12, or no view). Tried once a surface.
uint32_t SurfaceStencilSlotLocked(HostSurface *surface) {
  if (!surface || !surface->texture || !UsesVulkanBindings() || !plume::RenderFormatIsStencil(surface->format))
    return kInvalidDescriptor;
  if (surface->stencil_slot != UINT32_MAX)
    return surface->stencil_slot;
  surface->stencil_slot = kInvalidDescriptor;
  surface->stencil_view = surface->texture->createTextureView(
      plume::RenderTextureViewDesc::Texture2D(plume::RenderFormat::S8_UINT));
  if (!surface->stencil_view)
    return kInvalidDescriptor;
  const uint32_t slot = Video::AllocateTextureSlotLocked();
  if (slot == kInvalidDescriptor)
    return kInvalidDescriptor;
  Video::SetTextureSlotLocked(slot, surface->texture.get(), surface->stencil_view.get());
  surface->stencil_slot = slot;
  return slot;
}

void TransitionSurfaceLocked(HostSurface *surface, plume::RenderTextureLayout layout) {
  if (!surface || !surface->texture || surface->layout == layout)
    return;
  ++g_layout_epoch;  // banjo-nx (2026-10-26, render/textures.h)
  // banjo-nx (2026-10-19, banjo_deko3d_barrier_lean): from one read layout to another: no barrier (textures.cpp).
  if (IsDeko3D() && banjo::renut_gpu::Deko3DBarrierLeanOn() && ReadOnlyLayout(surface->layout) &&
      ReadOnlyLayout(layout)) {
    surface->layout = layout;
    return;
  }
  plume::RenderCommandList *cmd = Video::CommandListLocked();
  if (!cmd)
    return;
  cmd->barriers(layout == plume::RenderTextureLayout::COPY_SOURCE ||
                        layout == plume::RenderTextureLayout::COPY_DEST ||
                        layout == plume::RenderTextureLayout::RESOLVE_SOURCE ||
                        layout == plume::RenderTextureLayout::RESOLVE_DEST
                    ? plume::RenderBarrierStage::COPY
                    : plume::RenderBarrierStage::GRAPHICS,
                plume::RenderTextureBarrier(surface->texture.get(), layout));
  surface->layout = layout;
}

void SwapDepthImageLocked(HostSurface &surface, DepthImage &image) {
  ++g_surface_epoch;
  std::swap(surface.texture, image.texture);
  std::swap(surface.layout, image.layout);
  std::swap(surface.generation, image.generation);
  std::swap(surface.sample_view, image.sample_view);
  std::swap(surface.sample_slot, image.sample_slot);
  std::swap(surface.stencil_view, image.stencil_view);
  std::swap(surface.stencil_slot, image.stencil_slot);
  std::swap(surface.handover_views, image.views);
  image.host_width = surface.scale.Size(surface.width);
  image.host_height = surface.scale.Size(surface.height);
  image.format = surface.format;
  image.samples = surface.samples;
  image.format_word = surface.format_word;
}

bool MakeDepthImageLocked(const HostSurface &surface, DepthImage &image) {
  plume::RenderDevice *device = Video::Device();
  const uint32_t w = surface.scale.Size(surface.width), h = surface.scale.Size(surface.height);
  image.texture = device->createTexture(
      plume::RenderTextureDesc::DepthTarget(w, h, surface.format, plume::RenderMultisampling(surface.samples)));
  if (!TextureCreated(image.texture.get())) {
    image.texture.reset();
    ReportDeviceLost(device);
    return false;
  }
  image.texture->setName("renut depth surface (hand-over)");
  image.layout = plume::RenderTextureLayout::UNKNOWN;
  image.generation = g_next_generation++;
  image.host_width = w;
  image.host_height = h;
  image.format = surface.format;
  image.samples = surface.samples;
  image.format_word = surface.format_word;
  return true;
}

void RetireDepthImageLocked(DepthImage &image) {
  // The framebuffers a surface made over this image (cached by its generation).
  for (size_t i = 0; i < g_framebuffers.size();) {
    if (g_framebuffers[i].key.depth && g_framebuffers[i].key.depth_generation == image.generation) {
      Video::RetireLocked(std::move(g_framebuffers[i].framebuffer));
      g_framebuffers[i] = std::move(g_framebuffers.back());
      g_framebuffers.pop_back();
      ++g_framebuffer_epoch;
    } else {
      ++i;
    }
  }
  FreeDepthViews(image.views);
  if (image.sample_slot != UINT32_MAX && image.sample_slot != kInvalidDescriptor)
    Video::FreeTextureSlotLocked(image.sample_slot);
  image.sample_slot = UINT32_MAX;
  Video::RetireLocked(std::move(image.sample_view));
  if (image.stencil_slot != UINT32_MAX && image.stencil_slot != kInvalidDescriptor)
    Video::FreeTextureSlotLocked(image.stencil_slot);
  image.stencil_slot = UINT32_MAX;
  Video::RetireLocked(std::move(image.stencil_view));
  Video::RetireLocked(std::move(image.texture));
}

void TransitionDepthImageLocked(DepthImage &image, plume::RenderTextureLayout layout) {
  if (!image.texture || image.layout == layout)
    return;
  ++g_layout_epoch;  // banjo-nx (2026-10-26, render/textures.h)
  if (IsDeko3D() && banjo::renut_gpu::Deko3DBarrierLeanOn() && ReadOnlyLayout(image.layout) && ReadOnlyLayout(layout)) {
    image.layout = layout;
    return;
  }
  plume::RenderCommandList *cmd = Video::CommandListLocked();
  if (!cmd)
    return;
  cmd->barriers(plume::RenderBarrierStage::GRAPHICS, plume::RenderTextureBarrier(image.texture.get(), layout));
  image.layout = layout;
}

uint32_t DepthImageViewSlotLocked(plume::RenderTexture *texture, plume::RenderFormat format,
                                  std::vector<DepthImage::View> &views, const plume::RenderComponentMapping &mapping) {
  if (!texture)
    return kInvalidDescriptor;
  for (const DepthImage::View &v : views) {
    if (v.mapping.r == mapping.r && v.mapping.g == mapping.g && v.mapping.b == mapping.b && v.mapping.a == mapping.a)
      return v.slot;
  }
  // The depth format views the depth plane (deko3d samples it in the red channel), with the destination's mapping.
  plume::RenderTextureViewDesc desc = plume::RenderTextureViewDesc::Texture2D(format);
  desc.componentMapping = mapping;
  DepthImage::View v;
  v.mapping = mapping;
  v.view = texture->createTextureView(desc);
  if (!v.view)
    return kInvalidDescriptor;
  v.slot = Video::AllocateTextureSlotLocked();
  if (v.slot == kInvalidDescriptor) {
    Video::RetireLocked(std::move(v.view));
    return kInvalidDescriptor;
  }
  Video::SetTextureSlotLocked(v.slot, texture, v.view.get());
  views.push_back(std::move(v));
  return views.back().slot;
}

plume::RenderFramebuffer *GetFramebufferLocked(HostSurface *const *colors, uint32_t color_count,
                                               HostSurface *depth) {
  FramebufferKey key;
  for (uint32_t i = 0; i < color_count && i < d3d::kMaxRenderTargets; ++i) {
    key.colors[i] = colors[i];
    key.color_generations[i] = colors[i] ? colors[i]->generation : 0;
  }
  key.depth = depth;
  key.depth_generation = depth ? depth->generation : 0;
  // banjo-nx (2026-10-19, render-thread proposal 8): the recent ones first, the found one moved to the front.
  for (uint32_t i = 0; i < 4; ++i) {
    RecentFramebuffer &r = g_recent_framebuffers[i];
    if (r.epoch == g_framebuffer_epoch && r.framebuffer && r.key == key) {
      plume::RenderFramebuffer *found = r.framebuffer;
      if (i) {
        const RecentFramebuffer hit = r;
        for (uint32_t j = i; j > 0; --j)
          g_recent_framebuffers[j] = g_recent_framebuffers[j - 1];
        g_recent_framebuffers[0] = hit;
      }
      return found;
    }
  }
  auto remember = [&key](plume::RenderFramebuffer *framebuffer) {
    for (uint32_t j = 3; j > 0; --j)
      g_recent_framebuffers[j] = g_recent_framebuffers[j - 1];
    g_recent_framebuffers[0] = {key, framebuffer, g_framebuffer_epoch};
    return framebuffer;
  };
  for (auto &cached : g_framebuffers) {
    if (cached.key == key)
      return remember(cached.framebuffer.get());
  }

  const plume::RenderTexture *attachments[d3d::kMaxRenderTargets] = {};
  uint32_t count = 0;
  for (uint32_t i = 0; i < color_count && i < d3d::kMaxRenderTargets; ++i) {
    if (!colors[i] || !colors[i]->texture)
      break;  // host framebuffers need contiguous color attachments
    attachments[count++] = colors[i]->texture.get();
  }
  plume::RenderFramebufferDesc desc(attachments, count,
                                    depth && depth->texture ? depth->texture.get() : nullptr);
  auto framebuffer = Video::Device()->createFramebuffer(desc);
  if (!framebuffer) {
    RGPU_ERROR("Framebuffer creation failed ({} color targets, depth {})", count, depth != nullptr);
    return nullptr;
  }
  g_framebuffers.push_back({key, std::move(framebuffer)});
  return remember(g_framebuffers.back().framebuffer.get());
}

} // namespace renut::gpu
