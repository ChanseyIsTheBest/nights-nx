// nights-nx (2026-10-29): 60 frames a second on the screen with the game at its 30 (frame_double.h).
#include "renut_engine/gpu/backend/frame_double.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>

#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/render/surfaces.h"

REXCVAR_DEFINE_BOOL(nights_present_60, false, "NiGHTS",
                    "60 frames a second on the screen with the game at its own 30: after each game frame the render "
                    "thread draws it again with the vertex shader constants (camera, objects, bones) moved half a "
                    "frame further along their path from the frame before, and presents that half a frame later. "
                    "The game's speed and input are untouched. Changes the picture (the extra frames are predicted); "
                    "geometry the game writes each frame (particles) still moves at 30")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(nights_present_60_jump, 0.25, "NiGHTS",
                      "nights_present_60: a constant register is moved only when it changed by at most this share of "
                      "its size since the frame before (larger changes are cuts, flags or wraps: kept as they are)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(nights_present_60_hints, true, "NiGHTS",
                    "nights_present_60: the extra frame's draws tell the renderer which device chunks changed since "
                    "the draw before (as the game's own draws do), so they take deko3d's fast path; false = every "
                    "extra draw rebuilds its whole state")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(nights_present_60_test_delay_ms, 0, "NiGHTS",
                     "Test only: each extra frame's drawing takes this many ms longer (to see nights_present_60 pause "
                     "itself where the render thread cannot keep up)")
    .range(0, 100)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(nights_present_60_mode, 0, "NiGHTS",
                     "nights_present_60: 0 = predict (the game's frame, then one predicted half a frame on: two renders "
                     "a game frame, no added latency, overshoots a sharp turn by half a frame); 1 = interpolate (the "
                     "frame halfway between the last two, then the game's own: three renders, the game's frame shown "
                     "half a frame later, no overshoot)")
    .range(0, 1)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace renut::gpu::frame_double {

namespace {

constexpr u32 kVertexConstants = u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, VertexShaderF));
constexpr u32 kVertexConstantBytes = 256 * 16;
static_assert(kVertexConstants + kVertexConstantBytes <= d3d::kDeviceSnapshotSize);

enum class Kind : u8 { kDraw, kClear, kResolve, kTiling };

struct Op {
  Kind kind = Kind::kDraw;
  std::vector<u8> state;  // kDeviceSnapshotSize bytes (draw, clear, resolve)
  DrawCall call;
  std::vector<u8> vertices, indices;
  uint64_t key = 0;  // DrawKey
  u32 vs = 0;        // the vertex shader's guest address
  u32 flags = 0, stencil = 0, dest_texture_va = 0, dest_level = 0, dest_slice = 0;
  i32 dest_x = 0, dest_y = 0;
  float depth = 0.0f, color[4] = {};
  bool has_rect = false, has_color = false, has_dest_format = false;
  ClearRect rect{};
  d3d::TextureFetchConstant dest_format{};
  std::vector<ClearRect> tiles;  // kTiling; 'flags' / 'stencil' hold the extent's width / height
};

struct Frame {
  std::vector<Op> ops;
  u32 used = 0;  // ops[0, used) are this frame's (the rest keep their storage for the next ones)
  std::unordered_map<uint64_t, std::vector<u32>> draws;  // key -> the draws with it, in order
  // nights-nx (2026-10-29, the console's flicker): for each vertex shader, a draw of it and the registers that are
  // the same in all its draws of the frame (the camera and the like): what a repeated draw may move.
  struct Shared {
    u32 rep = 0;
    uint64_t same[4] = {};
  };
  std::unordered_map<u32, Shared> shaders;
  void Reset() {
    used = 0;
    for (auto &entry : draws)
      entry.second.clear();
    shaders.clear();
  }
  Op &Next(Kind kind) {
    if (used == ops.size())
      ops.emplace_back();
    Op &op = ops[used++];
    op.kind = kind;
    return op;
  }
};

// nights-nx (2026-10-29, the console's snaps): three frames, so a register is moved only while its motion is steady.
Frame g_frames[3];
u32 g_current = 0;  // g_frames[g_current] is logging; (g_current + 2) % 3 the frame before, (g_current + 1) % 3 the one before that
u32 g_have_previous = 0;  // how many frames before this one are logged (up to 2)
std::chrono::steady_clock::time_point g_last_present{};
double g_period_ms = 1000.0 / 30.0;
// The second present of a game frame, due half a frame after the first (render thread only).
bool g_pending = false;
std::chrono::steady_clock::time_point g_due{};
std::function<void()> g_present;
// Back-off: doubling stops for a while when the game's frames come late with it (the render thread cannot keep up,
// and the game, which steps per frame, would slow down): PlanFrame.
constexpr double kLateMs = 35.0;  // the game's 33.3 ms frame plus 5 %
std::chrono::steady_clock::time_point g_resume{};
double g_pause_s = 2.0;
uint32_t g_late = 0;
bool g_doubled_last = false;
uint64_t g_backoffs = 0;
double g_replay_ms = 0.0;  // the report's: time in Draw, summed over its window
// The report every 300 game frames.
uint64_t g_registers_unsteady = 0, g_draws_paired = 0;
uint64_t g_frames_seen = 0, g_frames_doubled = 0, g_draws = 0, g_draws_matched = 0, g_draws_loose = 0,
         g_registers_moved = 0,
         g_registers_kept = 0;

uint64_t Mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  return h * 0xFF51AFD7ED558CCDull;
}

u32 Word(const u8 *state, u32 offset) {
  u32 w;
  std::memcpy(&w, state + offset, 4);
  return w;  // as stored (guest order): only compared
}

// Which draw of the previous frame a draw is: its shaders, stream 0, indices, target and arguments.
uint64_t DrawKey(const u8 *state, const DrawCall &call) {
  uint64_t h = 0x84222325CBF29CE4ull;
  h = Mix(h, Word(state, d3d::kDeviceOffsetVertexShader));
  h = Mix(h, Word(state, d3d::kDeviceOffsetPixelShader));
  h = Mix(h, Word(state, d3d::kDeviceOffsetIndexBuffer));
  h = Mix(h, Word(state, d3d::kDeviceOffsetStreamSources));
  h = Mix(h, Word(state, d3d::kDeviceOffsetRenderTargets));
  const u32 fetch0 = u32(offsetof(d3d::Device, m_Constants) + offsetof(d3d::Constants, VertexFetch));
  h = Mix(h, Word(state, fetch0));
  h = Mix(h, (uint64_t(call.primitive) << 32) | call.count);
  h = Mix(h, (uint64_t(call.start_index) << 32) | call.start_vertex);
  h = Mix(h, (uint64_t(u32(call.base_vertex)) << 32) | (call.indexed ? 1u : 0u) | (call.up_stride << 1));
  return h;
}

float GuestFloat(const u8 *p) {
  u32 w;
  std::memcpy(&w, p, 4);
  w = __builtin_bswap32(w);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}

void PutGuestFloat(u8 *p, float f) {
  u32 w;
  std::memcpy(&w, &f, 4);
  w = __builtin_bswap32(w);
  std::memcpy(p, &w, 4);
}

// The vertex shader constants of 'cur' moved by 'step' times their change since 'prev' (0.5: half a frame on; -0.5:
// halfway back to prev), in place; each register's bytes before go to 'undo' (put back after the draw: the logged
// state stays the frame's own for the next frame's matching). Saves copying the whole 13 KB state a draw.
struct Undo {
  u32 at;
  u8 bytes[16];
};
// 'only': the registers it may move (bit r of word r / 64), or nullptr for all. 'before': the same draw two frames back:
// predicting, a register's step is scaled by how steady its motion is over the three frames (below). The console's
// snaps (Claris in the Dreams menu every 17 frames: her idle animation looping) were the prediction carrying such a jump
// half a frame further.
void Shifted(u8 *cur, const u8 *prev, const u8 *before, std::vector<Undo> &undo, float jump, float step,
             const uint64_t *only) {
  if (std::memcmp(cur + kVertexConstants, prev + kVertexConstants, kVertexConstantBytes) == 0)
    return;
  for (u32 r = 0; r < 256; ++r) {
    if (only && !((only[r / 64] >> (r % 64)) & 1))
      continue;
    const u32 at = kVertexConstants + r * 16;
    if (std::memcmp(cur + at, prev + at, 16) == 0)
      continue;
    float c[4], p[4], q[4];
    float size = 0.0f, change = 0.0f, change_before = 0.0f, jerk = 0.0f;
    bool finite = true;
    for (u32 i = 0; i < 4; ++i) {
      c[i] = GuestFloat(cur + at + i * 4);
      p[i] = GuestFloat(prev + at + i * 4);
      q[i] = GuestFloat(before + at + i * 4);
      finite = finite && std::isfinite(c[i]) && std::isfinite(p[i]) && std::isfinite(q[i]);
      size = std::max(size, std::max(std::fabs(c[i]), std::fabs(p[i])));
      change = std::max(change, std::fabs(c[i] - p[i]));
      change_before = std::max(change_before, std::fabs(p[i] - q[i]));
      jerk = std::max(jerk, std::fabs((c[i] - p[i]) - (p[i] - q[i])));
    }
    if (!finite || change > jump * size + 1e-6f) {
      ++g_registers_kept;
      continue;
    }
    // nights-nx (2026-10-29, the third console test: NiGHTS herself left at 30 in flight, held back as "unsteady" while
    // she turned and sped up): predicting, the step is scaled by how steady the motion is (all of it for a steady one,
    // part for a turn or a change of speed, none where the change is as large as the motion itself: the jerk of an
    // animation looping round); interpolating, it is never needed (the frame halfway lies between two of the game's
    // own frames: nothing overshoots).
    float scale = 1.0f;
    if (step > 0.0f) {
      const float motion = std::max(change, change_before);
      scale = motion > 1e-6f ? std::max(0.0f, 1.0f - jerk / motion) : 1.0f;
      if (scale <= 0.0f) {
        ++g_registers_unsteady;
        continue;
      }
    }
    Undo &u = undo.emplace_back();
    u.at = at;
    std::memcpy(u.bytes, cur + at, 16);
    for (u32 i = 0; i < 4; ++i)
      PutGuestFloat(cur + at + i * 4, c[i] + (c[i] - p[i]) * step * scale);
    ++g_registers_moved;
  }
}

// nights-nx (2026-10-29, the second console test: the scenery at 30): the instances of a repeated draw (trees,
// flowers, rocks, rings, chips: one mesh and its shaders many times) are paired with the frame before by their own
// constants. The signature is the registers that differ between the group's draws in this frame (its first
// kSignatureRegisters); an instance's pair is the draw of the same kind whose signature is nearest, taken only when it
// is clearly nearer than the next one (half the distance or less) or exactly the same. A static instance changes by
// the camera's step from frame to frame, much less than the distance to its neighbours; where that is not so (the
// balls of a ring seen from close by) the pair is refused and the instance falls back to the shared registers.
constexpr u32 kSignatureRegisters = 8;
constexpr size_t kMaxPairings = 40000;  // instances times candidates in a group, beyond which it is not paired
struct Signature {
  u32 regs[kSignatureRegisters];
  u32 count = 0;
};

Signature GroupSignature(const std::vector<Op> &ops, const std::vector<u32> &group) {
  Signature s;
  const u8 *first = ops[group[0]].state.data() + kVertexConstants;
  for (u32 r = 0; r < 256 && s.count < kSignatureRegisters; ++r)
    for (size_t k = 1; k < group.size(); ++k)
      if (std::memcmp(first + r * 16, ops[group[k]].state.data() + kVertexConstants + r * 16, 16) != 0) {
        s.regs[s.count++] = r;
        break;
      }
  return s;
}

float SignatureDistance(const u8 *a, const u8 *b, const Signature &s) {
  float d = 0.0f;
  for (u32 i = 0; i < s.count; ++i)
    for (u32 c = 0; c < 4; ++c) {
      const u32 at = kVertexConstants + s.regs[i] * 16 + c * 4;
      const float x = GuestFloat(a + at), y = GuestFloat(b + at);
      if (!std::isfinite(x) || !std::isfinite(y))
        return std::numeric_limits<float>::infinity();
      d += std::fabs(x - y);
    }
  return d;
}

// The draw of 'candidates' (indices into 'ops') that 'state' is, or nullptr.
const u8 *Pair(const u8 *state, const std::vector<Op> &ops, const std::vector<u32> &candidates, const Signature &s) {
  float best = std::numeric_limits<float>::infinity(), second = best;
  const u8 *found = nullptr;
  for (const u32 j : candidates) {
    const float d = SignatureDistance(state, ops[j].state.data(), s);
    if (d < best) {
      second = best;
      best = d;
      found = ops[j].state.data();
    } else if (d < second) {
      second = d;
    }
  }
  if (!found || !std::isfinite(best))
    return nullptr;
  return best == 0.0f || best <= 0.5f * second ? found : nullptr;
}

// The chain chunks (render/draw.h ChainChunkOf: 64-byte pieces of the device outside the float constants) that
// differ between two draws' states, as the snapshot deltas give them for the game's own draws.
void ChangedChunks(const u8 *a, const u8 *b, uint64_t changed[2]) {
  changed[0] = changed[1] = 0;
  for (u32 c = 0; c < 128; ++c) {
    const u32 at = c < kChainHeadChunks ? c * 64 : kChainTailBegin + (c - kChainHeadChunks) * 64;
    if (at >= d3d::kDeviceSnapshotSize)
      break;
    const u32 n = std::min<u32>(64, d3d::kDeviceSnapshotSize - at);
    if (std::memcmp(a + at, b + at, n) != 0)
      changed[c / 64] |= uint64_t(1) << (c % 64);
  }
}

}  // namespace

bool On() {
  static const bool on = REXCVAR_GET(nights_present_60);
  return on;
}

void LogDraw(const u8 *device_state, const DrawCall &call, const u8 *vertices, u32 vertex_bytes, const u8 *indices,
             u32 index_bytes) {
  Frame &f = g_frames[g_current];
  Op &op = f.Next(Kind::kDraw);
  op.state.assign(device_state, device_state + d3d::kDeviceSnapshotSize);
  op.call = call;
  op.vertices.assign(vertices, vertices + (vertices ? vertex_bytes : 0));
  op.indices.assign(indices, indices + (indices ? index_bytes : 0));
  op.key = DrawKey(device_state, call);
  op.vs = Word(device_state, d3d::kDeviceOffsetVertexShader);
  f.draws[op.key].push_back(f.used - 1);
  // The registers the same in every draw of this vertex shader so far.
  auto [it, fresh] = f.shaders.try_emplace(op.vs);
  Frame::Shared &s = it->second;
  if (fresh) {
    s.rep = f.used - 1;
    for (uint64_t &w : s.same)
      w = ~uint64_t(0);
  } else {
    const u8 *a = f.ops[s.rep].state.data() + kVertexConstants;
    const u8 *b = device_state + kVertexConstants;
    for (u32 w = 0; w < 4; ++w)
      for (uint64_t bits = s.same[w]; bits; bits &= bits - 1) {
        const u32 r = w * 64 + u32(__builtin_ctzll(bits));
        if (std::memcmp(a + r * 16, b + r * 16, 16) != 0)
          s.same[w] &= ~(uint64_t(1) << (r % 64));
      }
  }
}

void LogClear(const u8 *device_state, u32 flags, const ClearRect *rect, const float color[4], float depth,
              u32 stencil) {
  Op &op = g_frames[g_current].Next(Kind::kClear);
  op.state.assign(device_state, device_state + d3d::kDeviceSnapshotSize);
  op.flags = flags;
  op.has_rect = rect != nullptr;
  op.rect = rect ? *rect : ClearRect{};
  std::memcpy(op.color, color, sizeof(op.color));
  op.depth = depth;
  op.stencil = stencil;
}

void LogResolve(const u8 *device_state, u32 flags, const ClearRect *source_rect, u32 dest_texture_va,
                const d3d::TextureFetchConstant *dest_format, i32 dest_x, i32 dest_y, u32 dest_level,
                u32 dest_slice, const float clear_color[4], float clear_depth, u32 clear_stencil) {
  Op &op = g_frames[g_current].Next(Kind::kResolve);
  op.state.assign(device_state, device_state + d3d::kDeviceSnapshotSize);
  op.flags = flags;
  op.has_rect = source_rect != nullptr;
  op.rect = source_rect ? *source_rect : ClearRect{};
  op.dest_texture_va = dest_texture_va;
  op.has_dest_format = dest_format != nullptr;
  if (dest_format)
    op.dest_format = *dest_format;
  op.dest_x = dest_x;
  op.dest_y = dest_y;
  op.dest_level = dest_level;
  op.dest_slice = dest_slice;
  op.has_color = clear_color != nullptr;
  if (clear_color)
    std::memcpy(op.color, clear_color, sizeof(op.color));
  op.depth = clear_depth;
  op.stencil = clear_stencil;
}

void LogTiling(u32 width, u32 height, const ClearRect *tiles, u32 count) {
  Op &op = g_frames[g_current].Next(Kind::kTiling);
  op.flags = width;
  op.stencil = height;
  op.tiles.assign(tiles, tiles + (tiles ? count : 0));
}

Plan PlanFrame() {
  const auto now = std::chrono::steady_clock::now();
  const double since = g_last_present.time_since_epoch().count()
                           ? std::chrono::duration<double, std::milli>(now - g_last_present).count()
                           : 1000.0;
  g_last_present = now;
  ++g_frames_seen;
  // The game frame's length, smoothed (the half-frame time of the second present); a long one (loading, a stall) gets
  // no extra frame and counts for nothing else.
  const bool doubled_before = g_doubled_last;
  g_doubled_last = false;
  // (150 ms: a frame the doubling itself made late takes 40-60 ms on a loaded render thread, and must count as late.)
  if (since > 150.0) {
    g_late = 0;
    return Plan::kNone;
  }
  g_period_ms = g_period_ms * 0.9 + std::min(since, 50.0) * 0.1;
  if (g_have_previous < 2 || g_frames[g_current].used == 0 || now < g_resume)
    return Plan::kNone;
  // Late frames while doubling (eight in a row): the render thread cannot do both, so the game would slow down. Pause
  // 2 s; again within 10 s of resuming: twice as long, up to 32 s.
  g_late = doubled_before && since > kLateMs ? g_late + 1 : 0;
  if (g_late >= 8) {
    g_pause_s = now - g_resume < std::chrono::seconds(10) ? std::min(g_pause_s * 2.0, 32.0) : 2.0;
    g_resume = now + std::chrono::milliseconds(int64_t(g_pause_s * 1000.0));
    g_late = 0;
    ++g_backoffs;
    return Plan::kNone;
  }
  g_doubled_last = true;
  ++g_frames_doubled;
  static const int mode = REXCVAR_GET(nights_present_60_mode);
  return mode == 1 ? Plan::kInterpolate : Plan::kPredict;
}

void Draw(Shift shift) {
  static const float jump = float(REXCVAR_GET(nights_present_60_jump));
  Frame &cur = g_frames[g_current];
  const Frame &prev = g_frames[(g_current + 2) % 3];
  const Frame &prev2 = g_frames[(g_current + 1) % 3];
  static std::vector<Undo> undo;
  std::unordered_map<uint64_t, Signature> signatures;  // a repeated kind's signature, made once a frame
  static const bool hints = REXCVAR_GET(nights_present_60_hints);
  const u8 *drawn_before = nullptr;  // the state of the extra frame's draw before this one (nullptr: none yet)
  uint64_t seq_before = 0;
  const bool count = shift != Shift::kExact;  // the report counts the shifted pass once
  const auto start = std::chrono::steady_clock::now();
  struct Timed {
    std::chrono::steady_clock::time_point start;
    ~Timed() { g_replay_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
  } timed{start};
  static const int test_delay = REXCVAR_GET(nights_present_60_test_delay_ms);
  if (test_delay > 0 && count) [[unlikely]]
    std::this_thread::sleep_for(std::chrono::milliseconds(test_delay));
  std::lock_guard lock(State().mutex);
  for (u32 i = 0; i < cur.used; ++i) {
    Op &op = cur.ops[i];
    switch (op.kind) {
    case Kind::kDraw: {
      g_draws += count;
      const u8 *state = op.state.data();
      if (shift != Shift::kExact) {
        // nights-nx (2026-10-29, the console's flicker: the rings' balls scattered in every extra frame): a draw is
        // moved whole only when it is the one draw with its key in both frames. A repeated one (the balls of a ring,
        // the chips: the same mesh and shaders many times, in an order and a number that change) cannot be paired
        // with its draw of the frame before: only the registers its vertex shader has the same in all its draws of
        // both frames move (the camera and the like); its own stay as the game's frame has them.
        const float step = shift == Shift::kPredict ? 0.5f : -0.5f;
        auto one = [&](const Frame &f) -> const u8 * {  // the one draw with this key in f, or nullptr
          const auto it = f.draws.find(op.key);
          return it != f.draws.end() && it->second.size() == 1 ? f.ops[it->second[0]].state.data() : nullptr;
        };
        // A repeated draw's pair in the frame before, and that one's in the frame before it (the same signature: the
        // registers that differ between this frame's draws of the kind).
        const auto group = cur.draws.find(op.key);
        Signature sig;
        if (group != cur.draws.end() && group->second.size() > 1) {
          auto [it, fresh] = signatures.try_emplace(op.key);
          if (fresh)
            it->second = GroupSignature(cur.ops, group->second);
          sig = it->second;
        }
        auto PairedBefore = [&](const Op &o) -> const u8 * {
          const auto there = prev.draws.find(o.key);
          if (group == cur.draws.end() || there == prev.draws.end() ||
              group->second.size() * there->second.size() > kMaxPairings)
            return nullptr;
          return Pair(o.state.data(), prev.ops, there->second, sig);
        };
        auto PairedBefore2 = [&](const Op &o, const u8 *was) -> const u8 * {
          const auto there2 = prev2.draws.find(o.key);
          if (there2 == prev2.draws.end() || group->second.size() * there2->second.size() > kMaxPairings)
            return nullptr;
          return Pair(was, prev2.ops, there2->second, sig);
        };
        undo.clear();
        const auto mine = cur.draws.find(op.key);
        const u8 *was = one(prev), *was2 = one(prev2);
        if (mine != cur.draws.end() && mine->second.size() == 1 && was && was2) {
          ++g_draws_matched;
          Shifted(op.state.data(), was, was2, undo, jump, step, nullptr);
        } else if (const u8 *paired = PairedBefore(op), *paired2 = paired ? PairedBefore2(op, paired) : nullptr;
                   paired && paired2) {
          ++g_draws_paired;
          Shifted(op.state.data(), paired, paired2, undo, jump, step, nullptr);
        } else {
          const auto here = cur.shaders.find(op.vs);
          const auto there = prev.shaders.find(op.vs);
          const auto there2 = prev2.shaders.find(op.vs);
          if (here != cur.shaders.end() && there != prev.shaders.end() && there2 != prev2.shaders.end()) {
            ++g_draws_loose;
            uint64_t only[4];
            for (u32 w = 0; w < 4; ++w)
              only[w] = here->second.same[w] & there->second.same[w] & there2->second.same[w];
            Shifted(op.state.data(), prev.ops[there->second.rep].state.data(),
                    prev2.ops[there2->second.rep].state.data(), undo, jump, step, only);
          }
        }
      }
      DrawCall call = op.call;
      call.up_vertices = op.vertices.empty() ? nullptr : op.vertices.data();
      call.up_indices = op.indices.empty() ? nullptr : op.indices.data();
      // The chain hint, as backend/queued.cpp gives the game's delta-chain draws: consecutive numbers, the chunks
      // that changed since the draw before (all for the first draw of the frame), their fields.
      if (hints) {
        g_chain_hint = {};
        g_chain_hint.seq = NextChainSeq();
        if (drawn_before && g_chain_hint.seq == seq_before + 1) {
          ChangedChunks(drawn_before, state, g_chain_hint.changed);
          g_chain_hint.fields = ChainFields(g_chain_hint.changed);
        } else {
          g_chain_hint.changed[0] = g_chain_hint.changed[1] = ~uint64_t(0);
          g_chain_hint.fields = kFieldsAll;
        }
        seq_before = g_chain_hint.seq;
        drawn_before = state;
      }
      DrawStateLocked(state, call);
      g_chain_hint = {};  // only for the draw itself
      for (const Undo &u : undo)
        std::memcpy(op.state.data() + u.at, u.bytes, 16);  // the frame's own state again
      undo.clear();
      break;
    }
    case Kind::kClear:
      ClearStateLocked(op.state.data(), op.flags, op.has_rect ? &op.rect : nullptr, op.color, op.depth, op.stencil);
      break;
    case Kind::kResolve:
      ResolveStateLocked(op.state.data(), op.flags, op.has_rect ? &op.rect : nullptr, op.dest_texture_va,
                         op.has_dest_format ? &op.dest_format : nullptr, op.dest_x, op.dest_y, op.dest_level,
                         op.dest_slice, op.has_color ? op.color : nullptr, op.depth, op.stencil);
      break;
    case Kind::kTiling:
      SetTilingExtentLocked(op.flags, op.stencil);
      SetTilesLocked(op.tiles.empty() ? nullptr : op.tiles.data(), u32(op.tiles.size()));
      break;
    }
  }
}

void SchedulePresent(std::function<void()> present) {
  g_present = std::move(present);
  g_due = g_last_present + std::chrono::microseconds(int64_t(g_period_ms * 500.0));
  g_pending = true;
}

bool PresentPending() { return g_pending; }

std::chrono::steady_clock::time_point PresentDue() { return g_due; }

void PresentIfDue(bool force) {
  if (!g_pending || (!force && std::chrono::steady_clock::now() < g_due))
    return;
  g_pending = false;
  g_present();
}

void EndFrame() {
  if (g_frames_seen && g_frames_seen % 300 == 0) {
    RGPU_INFO("[renut] nights_present_60 ({}): {} of {} game frames doubled (game frame {:.1f} ms); {} draws, {} "
              "moved whole (one draw of their kind in both frames), {} repeated ones paired by their constants, {} "
              "by their shader's shared registers only; {} constant registers moved, {} kept as unsteady (a jerk), {} "
              "kept "
              "(a jump); {} pauses for late frames so far; the extra drawing {:.2f} ms a doubled frame",
              REXCVAR_GET(nights_present_60_mode) == 1 ? "interpolate" : "predict", g_frames_doubled, g_frames_seen,
              g_period_ms, g_draws, g_draws_matched, g_draws_paired, g_draws_loose, g_registers_moved,
              g_registers_unsteady,
              g_registers_kept, g_backoffs, g_frames_doubled ? g_replay_ms / double(g_frames_doubled) : 0.0);
    g_frames_doubled = g_draws = g_draws_matched = g_draws_loose = g_registers_moved = g_registers_kept = 0;
    g_registers_unsteady = g_draws_paired = 0;
    g_replay_ms = 0.0;
    g_frames_seen = 0;
  }
  g_current = (g_current + 1) % 3;
  g_frames[g_current].Reset();
  g_have_previous = std::min<u32>(g_have_previous + 1, 2);
}

}  // namespace renut::gpu::frame_double
