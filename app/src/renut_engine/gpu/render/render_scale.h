/**
 * @file    gpu/render/render_scale.h
 * @brief   Render resolution: host pixels per guest pixel.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace renut::gpu {

// Host pixels per guest pixel of a render target, and of what resolves from
// it, as one of a fixed set of ratios: the render resolutions
// (renut_resolution: 1x, 480p, 1080p, 1440p, 4K) and the targets that render
// finer than the rest (graphics_options.h: shadow maps at 4x, ambient
// occlusion at twice the resolution: 4/3, 6x). Code 0 is 1x, the scale of all
// content that is not rendered (guest textures). Shaders get the code in the
// descriptor index (kResolutionScaleShift) and have the same factors
// (renutTextureScale in shader_common.h).
// banjo-nx (2026-10-21, internal resolution, the user's request): on deko3d codes 4 and 6 are the internal resolutions
// of handheld and docked mode instead of 3 (4K) and 6 (ambient occlusion at twice 4K), which the Switch never draws: any
// height (banjo_renut_render_height_handheld / _docked), height / 720 reduced, set once by LatchResolutionScaleLocked
// (SetInternalHeights) before any target is made. The shaders read the same heights from SharedConstants (v[84].w:
// handheld | docked << 16; tools/deko3d_glsl_opt.py pass scalecodes). The PC's back ends keep Renut's table (their
// shaders are made from shader_common.h).
struct RenderScale {
  static constexpr uint32_t kCount = 8;
#if defined(PLUME_DEKO3D)
  static inline uint32_t kNumerators[kCount] = {1, 2, 3, 2, 9, 4, 5, 4};
  static inline uint32_t kDenominators[kCount] = {1, 3, 2, 1, 10, 1, 4, 3};
#else
  static constexpr uint32_t kNumerators[kCount] = {1, 2, 3, 2, 3, 4, 6, 4};
  static constexpr uint32_t kDenominators[kCount] = {1, 3, 2, 1, 1, 1, 1, 3};
#endif
  // banjo-nx (2026-10-21): the codes of the internal resolutions (deko3d; on the PC 3x and 6x, as Renut's).
  static constexpr uint32_t kHandheldCode = 4, kDockedCode = 6;

  uint32_t code = 0;

  float Factor() const { return float(kNumerators[code]) / float(kDenominators[code]); }
  bool operator==(const RenderScale &) const = default;

  // A guest pixel edge in host pixels, to the nearest: rects that share an
  // edge in guest pixels share it in host pixels.
  int32_t Coord(int32_t guest) const {
    return int32_t(std::lround(double(guest) * kNumerators[code] / kDenominators[code]));
  }

  // A guest size in host pixels, 1 at least.
  uint32_t Size(uint32_t guest) const {
    return std::max<uint32_t>(uint32_t(Coord(int32_t(guest))), 1u);
  }

  // The scale of ratio numerator / denominator, if it is one of them.
  static bool FromRatio(uint32_t numerator, uint32_t denominator, RenderScale &out) {
    for (uint32_t c = 0; c < kCount; ++c) {
      if (uint64_t(numerator) * kDenominators[c] == uint64_t(kNumerators[c]) * denominator) {
        out.code = c;
        return true;
      }
    }
    return false;
  }

  // This scale times 'multiplier', or this one when that is not among them.
  RenderScale Times(uint32_t multiplier) const {
    RenderScale out = *this;
    FromRatio(kNumerators[code] * multiplier, kDenominators[code], out);
    return out;
  }
};

} // namespace renut::gpu
