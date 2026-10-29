/**
 * @file    gpu/d3d/hooks/shader.cpp
 * @brief   Shader creation. The renderer owns the shader objects: each is a
 *          record keyed to its XenosRecomp cache entry by container hash.
 *
 * The ~150 shaders N&B compiles from HLSL at boot (hcg*ShaderCreateByHlsl)
 * reach D3DDevice_CreateVertexShader/CreatePixelShader too, so a cache miss
 * here is captured to disk for the next shader cache build.
 *
 * Shaders loaded with asset bundles are D3D objects the bundle already holds,
 * set up in place by XGRegisterVertexShader/XGRegisterPixelShader. Those stay
 * guest objects; the host side is kept per object address, keyed by the same
 * container hash.
 */
#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "renut_engine/gpu/banjo_renut_select.h"
#include <rex/types.h>

#include "renut_engine/gpu/render/draw.h"
#include "renut_engine/gpu/guest/command_buffers.h"  // banjo-nx (2026-10-27): NoteShaderSortKey
#include "renut_engine/gpu/guest/guest_memory.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/guest/resources.h"
#include "renut_engine/gpu/render/pipeline.h"
#include "renut_engine/gpu/render/shader_library.h"
#include "renut_engine/gpu/backend/video.h"

using namespace renut::gpu;

REXCVAR_DEFINE_INT32(banjo_renut_shader_reuse, 128, "Banjo/Enhancements",
                     "Renut's renderer (2026-10-23, batch 6): the host shaders of this many shader objects the game "
                     "destroyed are kept, and given to the next object of the same shader instead of a new one (on "
                     "deko3d each new one unpacked and parsed its GLSL on the render thread: N&B destroys and makes "
                     "shader objects all the time while it streams the level). 0: none kept")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(banjo_renut_shared_host_shaders, true, "Banjo/Enhancements",
                    "Renut's renderer (2026-10-28, per-draw night): the live shader objects of one shader share one host "
                    "shader (N&B makes a shader object per mesh, many alive at once with the same shader: each unpacked "
                    "and parsed its GLSL on the render thread, kept its own copy of it and compiled its own variants). "
                    "false: one host shader an object, as before")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

// banjo-nx (2026-10-23, batch 6, banjo_renut_shader_reuse): host shaders of destroyed shader records, by their cache
// entry (one host shader is made from the entry alone: CreateCachedShader), the least recently kept going first. A host
// shader is immutable once made but for the variants it compiles; the pipelines keep their own compiled code and the
// compile jobs their own GLSL, so handing one to a new record is what making it again would give. State().mutex.
struct KeptShader {
  const ShaderCacheEntry *entry = nullptr;
  std::unique_ptr<plume::RenderShader> host;
  uint64_t stamp = 0;
};
// Never destroyed (left at exit, after the device is gone).
std::vector<KeptShader> &g_kept_shaders = *new std::vector<KeptShader>();
uint64_t g_kept_stamp = 0;

u32 KeptShaderCap() {
  static const u32 cap = u32(std::max(0, REXCVAR_GET(banjo_renut_shader_reuse)));
  return cap;
}

void KeepShader(const ShaderCacheEntry *entry, std::unique_ptr<plume::RenderShader> host) {
  const u32 cap = KeptShaderCap();
  if (!entry || !host || !cap)
    return;
  KeptShader *slot = nullptr;
  for (KeptShader &k : g_kept_shaders) {
    if (k.entry == entry) {  // one of each is enough
      slot = &k;
      break;
    }
  }
  if (!slot && g_kept_shaders.size() < cap)
    slot = &g_kept_shaders.emplace_back();
  if (!slot) {
    slot = &g_kept_shaders.front();
    for (KeptShader &k : g_kept_shaders)
      if (k.stamp < slot->stamp)
        slot = &k;
  }
  slot->entry = entry;
  slot->host = std::move(host);
  slot->stamp = ++g_kept_stamp;
}

std::unique_ptr<plume::RenderShader> TakeKeptShader(const ShaderCacheEntry *entry) {
  for (KeptShader &k : g_kept_shaders) {
    if (k.entry == entry && k.host) {
      k.entry = nullptr;
      k.stamp = 0;
      return std::move(k.host);
    }
  }
  return nullptr;
}

// banjo-nx (2026-10-28, per-draw night, banjo_renut_shared_host_shaders): the host shaders the live records use, with
// the count of records using each, keyed by the cache entry (by the record itself with the switch off). A record's
// 'host' points into it; its release (ReleaseHostShader) hands the last record's host to KeepShader. Its own lock (the
// records are made and released on the render thread and on the game's threads, under State().mutex mostly).
struct LiveHost {
  std::unique_ptr<plume::RenderShader> shader;
  u32 records = 0;
};
// Never destroyed (left at exit, after the device is gone), as g_kept_shaders.
std::unordered_map<const void *, LiveHost> &g_live_hosts = *new std::unordered_map<const void *, LiveHost>();
std::mutex g_live_hosts_mutex;
uint64_t g_hosts_made = 0, g_hosts_shared = 0, g_hosts_kept = 0;

bool SharedHostShadersOn() {
  static const bool on = REXCVAR_GET(banjo_renut_shared_host_shaders);
  return on;
}
const void *LiveHostKey(const ShaderRecord *record) {
  return SharedHostShadersOn() ? static_cast<const void *>(record->entry) : static_cast<const void *>(record);
}

// The last record's host to KeepShader; the record's pointer cleared. Under the record's host_mutex.
void ReleaseHostShader(ShaderRecord *record) {
  if (!record || !record->host)
    return;
  InvalidateHostPacketsLocked();  // banjo-nx (2026-10-28, host packets): a packet may hold this host shader
  std::unique_ptr<plume::RenderShader> last;
  {
    std::lock_guard lock(g_live_hosts_mutex);
    auto it = g_live_hosts.find(LiveHostKey(record));
    if (it != g_live_hosts.end() && it->second.shader.get() == record->host && --it->second.records == 0) {
      last = std::move(it->second.shader);
      g_live_hosts.erase(it);
    }
  }
  record->host = nullptr;
  KeepShader(record->entry, std::move(last));
}

// Where D3DDevice_CreateVertexShader / CreatePixelShader (0x8264E8B0 /
// 0x8264E6A8) lay out a shader object: the container's virtual part copied
// after the D3D header, and a pointer to its physical part (literal constants
// and microcode). Objects in bundles have the same layout.
constexpr u32 kVertexShaderContainer = 0x368;
constexpr u32 kVertexShaderPhysical = 0x20;
constexpr u32 kPixelShaderContainer = 0x28;
constexpr u32 kPixelShaderPhysical = 0x18;

// Guest shader objects registered in place, by object address. The render
// thread's (State().mutex).
std::unordered_map<u32, std::unique_ptr<ShaderRecord>> g_guest_shaders;

// Recent FindShaderLocked results (two lookups a draw otherwise, one under a
// shared lock). An entry goes when its shader does (ForgetShader): both kinds
// are destroyed on the render thread, the only user, so a memo'd record is
// alive. (Checking the record registry's generation instead missed whenever
// any record came or went, which busy levels do all the time.) State().mutex.
// A miss is kept too (record null): draws keep binding addresses that are no
// shader of the kind (depth-only passes), and those cost a registry lookup and
// a map search a draw. A shader made at an address forgets it first (both are
// in order with the draws on the render thread). va 0 is an empty entry
// (FindShaderLocked never looks it up).
struct ShaderMemo {
  u32 va = 0;
  ShaderRecord *record = nullptr;
};
// A table per kind, a set of four entries per cache line, the most recent
// first. A single direct-mapped table for both kinds had the shaders of a busy
// level (Showdown Town binds hundreds a frame) evicting each other every frame,
// and each miss was a registry lookup and a map search.
constexpr u32 kShaderMemoWays = 4;
// banjo-nx (2026-10-28, per-draw night): 4,096 sets a kind (1,024): N&B makes a shader object per mesh (69-72 % of the
// city's draws bind another address), more than the 4,096 entries a kind held; a miss is a registry lookup under its
// lock. 512 KB of static memory, not the heap.
constexpr u32 kShaderMemoSetBits = 12;
struct alignas(64) ShaderMemoSet {
  ShaderMemo way[kShaderMemoWays];
};
static_assert(sizeof(ShaderMemoSet) == 64);
ShaderMemoSet g_shader_memo[2][1u << kShaderMemoSetBits];

u32 KindIndex(RecordKind kind) {
  return kind == RecordKind::PixelShader ? 1 : 0;
}

ShaderMemoSet &ShaderMemoSetFor(u32 kind_index, u32 shader_va) {
  return g_shader_memo[kind_index][(shader_va * 0x9E3779B1u) >> (32 - kShaderMemoSetBits)];
}

// The last shader found of each kind: consecutive draws mostly keep their
// shaders, and this is one compare where the memo is a cache miss.
ShaderMemo g_last_shader[2];

void ForgetShader(u32 shader_va) {
  for (u32 kind = 0; kind < 2; ++kind) {
    for (ShaderMemo &memo : ShaderMemoSetFor(kind, shader_va).way) {
      if (memo.va == shader_va)
        memo = {};
    }
    if (g_last_shader[kind].va == shader_va)
      g_last_shader[kind] = {};
  }
}

} // namespace

namespace renut::gpu {

// banjo-nx (2026-10-23, render-thread round): the lines FindShaderLocked will read for the next draw's shader, fetched
// a draw ahead (command_buffers.cpp Replay): the memo set, or with the shader the last one, its record and entry.
// Prefetches only: the memo is not reordered.
void PrefetchShaderLocked(u32 shader_va, RecordKind kind) {
  if (!shader_va)
    return;
  const u32 kind_index = KindIndex(kind);
  const ShaderMemo &last = g_last_shader[kind_index];
  if (last.va == shader_va) {
    if (last.record) {
      __builtin_prefetch(last.record);
      __builtin_prefetch(reinterpret_cast<const char *>(last.record) + 64);  // the draw summary
      __builtin_prefetch(reinterpret_cast<const char *>(last.record) + 128);  // (its used words, 2026-10-27)
      __builtin_prefetch(last.record->entry);
    }
    return;
  }
  __builtin_prefetch(&ShaderMemoSetFor(kind_index, shader_va));
}

// banjo-nx (2026-10-27, the replay stream's second stage): when the memo already holds the shader (its set fetched an op
// before by PrefetchShaderLocked), the record's lines the draw reads first (the draw summary with them). A look only:
// the memo is not reordered and a shader not in it is left to FindShaderLocked.
void PrefetchShaderRecordLocked(u32 shader_va, RecordKind kind) {
  if (!shader_va)
    return;
  const u32 kind_index = KindIndex(kind);
  const ShaderRecord *record = nullptr;
  if (g_last_shader[kind_index].va == shader_va) {
    record = g_last_shader[kind_index].record;
  } else {
    for (const ShaderMemo &memo : ShaderMemoSetFor(kind_index, shader_va).way) {
      if (memo.va == shader_va) {
        record = memo.record;
        break;
      }
    }
  }
  if (record) {
    const char *r = reinterpret_cast<const char *>(record);
    __builtin_prefetch(r);
    __builtin_prefetch(r + 64);
    __builtin_prefetch(r + 128);
  }
}

void PrefetchShaderRecordAt(u32 shader_va) {
  if (const char *r = GuestPtr<const char>(shader_va)) {
    __builtin_prefetch(r);        // the cache entry, host shader, declaration pointers
    __builtin_prefetch(r + 64);   // the draw summary
    __builtin_prefetch(r + 128);  // (its used words, flags)
  }
}
void PrefetchShaderBehind(u32 shader_va) {
  const auto *r = GuestPtr<const ShaderRecord>(shader_va);
  if (!r || r->self_va != shader_va)
    return;  // not one of our records (a guest object's bytes)
  if (const char *e = reinterpret_cast<const char *>(r->entry)) {
    __builtin_prefetch(e);
    __builtin_prefetch(e + 64);
  }
  if (const char *d = reinterpret_cast<const char *>(r->bound_declaration.get())) {
    __builtin_prefetch(d);
    __builtin_prefetch(d + 64);
    __builtin_prefetch(d + 128);
  }
}

ShaderRecord *FindShaderLocked(u32 shader_va, RecordKind kind) {
  if (!shader_va)
    return nullptr;
  const u32 kind_index = KindIndex(kind);
  ShaderMemo &last = g_last_shader[kind_index];
  if (last.va == shader_va)
    return last.record;
  ShaderMemo *ways = ShaderMemoSetFor(kind_index, shader_va).way;
  u32 hit = 0;
  while (hit < kShaderMemoWays && ways[hit].va != shader_va)
    ++hit;
  ShaderMemo found;
  if (hit < kShaderMemoWays) {
    found = ways[hit];
  } else {
    hit = kShaderMemoWays - 1;  // the least recent goes
    ShaderRecord *record = FindRecord<ShaderRecord>(shader_va, kind);
    if (!record) {
      auto it = g_guest_shaders.find(shader_va);
      record = it != g_guest_shaders.end() && it->second->kind == kind ? it->second.get() : nullptr;
    }
    found = {shader_va, record};
  }
  for (u32 i = hit; i > 0; --i)
    ways[i] = ways[i - 1];
  ways[0] = found;
  last = found;
  return found.record;
}

void ReleaseGuestShaderLocked(u32 shader_va) {
  auto it = g_guest_shaders.find(shader_va);
  if (it == g_guest_shaders.end())
    return;
  if (ShaderRecord *record = it->second.get()) {  // banjo-nx (2026-10-23, batch 6): its host shader kept
    std::lock_guard shader_lock(record->host_mutex);
    ReleaseHostShader(record);  // banjo-nx (2026-10-28)
  }
  g_guest_shaders.erase(it);
  ForgetShader(shader_va);
}

void BuildDeclarationLayout(VertexDeclarationRecord &declaration) {
  declaration.layout =
      BuildHostVertexLayout(declaration.elements.data(), u32(declaration.elements.size()));
  // FNV-1a over what the input layout depends on.
  uint64_t hash = 0xCBF29CE484222325ull;
  auto mix = [&hash](uint64_t v) { hash = (hash ^ v) * 0x100000001B3ull; };
  for (const plume::RenderInputElement &e : declaration.layout.input_elements) {
    mix(e.location);
    mix(uint64_t(e.format));
    mix(e.slotIndex);
    mix(e.alignedByteOffset);
  }
  for (u32 s = 0; s < kMaxVertexStreams; ++s)
    mix(declaration.layout.streams[s].host_stride);
  declaration.layout_hash = hash;
  // banjo-nx (2026-10-23, batch 6): VertexDeclarationRecord::stream_order.
  u32 seen = 0;
  declaration.stream_count = 0;
  for (const GuestVertexElement &e : declaration.elements) {
    const u32 stream = u16(e.Stream);
    if (stream >= kMaxVertexStreams || (seen & (1u << stream)))
      continue;
    seen |= 1u << stream;
    declaration.stream_order[declaration.stream_count++] = u8(stream);
  }
}

const VertexDeclarationRecord *ShaderFetchDeclarationLocked(ShaderRecord &vs) {
  if (vs.fetch_declaration_built)
    return vs.fetch_declaration.get();
  vs.fetch_declaration_built = true;
  if (!vs.entry || !vs.entry->vertexInputCount)
    return nullptr;
  auto declaration = std::make_unique<VertexDeclarationRecord>();
  const ShaderVertexInput *inputs = g_activeShaderVertexInputs + vs.entry->vertexInputOffset;
  for (u32 i = 0; i < vs.entry->vertexInputCount; ++i) {
    const ShaderVertexInput &input = inputs[i];
    // Stream n's fetch constant is Xenos index 95 - n (d3d::StreamFetchIndex).
    const u32 first_stream_fetch = d3d::kFirstVertexFetchIndex;
    if (input.fetchIndex < first_stream_fetch || input.fetchIndex > 95)
      continue;
    const u32 stream = 95 - input.fetchIndex;
    const u32 type = FetchDeclType(input.format, input.flags & 1, input.flags & 2);
    if (type == kDeclTypeUnused)
      continue;
    GuestVertexElement element{};
    element.Stream = u16(stream);
    element.Offset = u16(input.offsetDwords * 4);
    element.Type = type;
    element.Method = 0;
    element.Usage = input.usage;
    element.UsageIndex = input.usageIndex;
    declaration->elements.push_back(element);
    if (stream < d3d::kMaxStreams && !vs.fetch_strides[stream])
      vs.fetch_strides[stream] = input.strideDwords * 4u;
  }
  if (declaration->elements.empty())
    return nullptr;
  BuildDeclarationLayout(*declaration);
  vs.fetch_declaration = std::move(declaration);
  return vs.fetch_declaration.get();
}

plume::RenderShader *GetHostShader(ShaderRecord *shader) {
  if (!shader || !shader->entry)
    return nullptr;
  // Built already: draws call this under State().mutex, which destruction
  // (DestroyShaderRecord) takes too, and only this function sets 'host'.
  if (plume::RenderShader *host = shader->host)
    return host;
  std::lock_guard lock(shader->host_mutex);
  if (!shader->host) {
    // banjo-nx (2026-10-28, per-draw night): a live record's host of the same entry first (banjo_renut_shared_host_shaders),
    // then a destroyed one's (batch 6), then a new one.
    std::lock_guard live_lock(g_live_hosts_mutex);
    LiveHost &live = g_live_hosts[LiveHostKey(shader)];
    uint64_t *count = &g_hosts_shared;
    if (!live.shader) {
      live.shader = TakeKeptShader(shader->entry);  // banjo-nx (2026-10-23, batch 6)
      count = &g_hosts_kept;
      if (!live.shader) {
        live.shader = CreateCachedShader(Video::Device(), *shader->entry);
        count = &g_hosts_made;
      }
    }
    if (!live.shader) {
      g_live_hosts.erase(LiveHostKey(shader));
      RGPU_ERROR("Creating shader {:016X} failed", shader->hash);
      shader->entry = nullptr;  // don't retry every draw
      return nullptr;
    }
    ++live.records;
    shader->host = live.shader.get();
    ++*count;
    const uint64_t n = g_hosts_made + g_hosts_shared + g_hosts_kept;
    if (n >= 256 && (n & (n - 1)) == 0)
      RGPU_INFO("[renut] host shaders: {} made, {} shared with a live object of the same shader "
                "(banjo_renut_shared_host_shaders), {} from a destroyed one (banjo_renut_shader_reuse); {} live",
                g_hosts_made, g_hosts_shared, g_hosts_kept, g_live_hosts.size());
  }
  return shader->host;
}

void DestroyVertexDeclarationRecord(VertexDeclarationRecord *declaration) {
  if (!declaration)
    return;
  std::lock_guard lock(State().mutex);
  InvalidateFastDrawForRecordLocked(declaration);  // banjo-nx (2026-10-28): it may hold this record
  ForgetVertexDeclarationLocked(declaration->self_va);
  DestroyRecord(declaration);
}

void DestroyShaderRecord(ShaderRecord *shader) {
  if (!shader)
    return;
  // Pipelines built from it keep their own compiled copy; only the module goes.
  {
    auto &s = State();
    std::lock_guard lock(s.mutex);
    InvalidateFastDrawForRecordLocked(shader);  // banjo-nx (2026-10-28): it may hold this record
    ForgetShader(shader->self_va);
    std::lock_guard shader_lock(shader->host_mutex);
    ReleaseHostShader(shader);  // banjo-nx (2026-10-23, batch 6; 2026-10-28: the registry's)
  }
  DestroyRecord(shader);
}

} // namespace renut::gpu

namespace {

u32 CreateShaderRecord(const be_u32 *function, bool pixel) {
  if (!function)
    return 0;
  const auto *container = reinterpret_cast<const uint8_t *>(function);
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  if ((u32(header->flags) & 0xFFFFFF00u) != 0x102A1100u) {
    RGPU_ERROR("Create{}Shader: not a shader container (flags {:08X})", pixel ? "Pixel" : "Vertex",
               u32(header->flags));
    return 0;
  }

  auto *shader = CreateRecord<ShaderRecord>(pixel ? RecordKind::PixelShader
                                                  : RecordKind::VertexShader);
  if (!shader)
    return 0;
  const auto type = pixel ? d3d::ResourceType::PixelShader : d3d::ResourceType::VertexShader;
  shader->x360.resource.Common = kCommonD3DCreated | u32(type);
  shader->x360.resource.ReferenceCount = 1u;

  shader->hash = HashShaderContainer(container);
  // nights-nx (from bejeweled3-nx): the container, for GetShaderFunction.
  shader->function.assign(container, container + u32(header->virtualSize) + u32(header->physicalSize));
  shader->entry = FindShaderEntry(shader->hash);
  if (!shader->entry)
    CaptureMissingShader(container, pixel, shader->hash);
  NoteShaderSortKey(shader->self_va, shader->hash, ShaderSortableByHash(shader->hash, shader->entry, pixel));  // 2026-10-27
  PipelineShaderLoaded(shader->entry);
  // Draws before it may have found no shader at the address (FindShaderLocked).
  render_thread::Queue([va = shader->self_va] { ForgetShader(va); });
  return shader->self_va;
}

// The host side of a shader object the guest set up in place: its container is
// the virtual part inside the object followed by the physical part.
void RegisterGuestShader(u32 shader_va, u32 physical_va, bool pixel) {
  const u32 container_va = shader_va + (pixel ? kPixelShaderContainer : kVertexShaderContainer);
  const auto *header = GuestPtr<const d3d::ShaderContainer>(container_va);
  const u8 *physical = physical_va < 0x20000000u ? GuestPhysicalPtr<const u8>(physical_va)
                                                 : GuestPtr<const u8>(physical_va);
  if (!header || !physical)
    return;
  const u32 flags = header->flags;
  const u32 virtual_size = header->virtualSize;
  const u32 physical_size = header->physicalSize;
  if ((flags & 0xFFFFFF00u) != 0x102A1100u || ((flags & 1) != 0) == pixel ||
      virtual_size < sizeof(d3d::ShaderContainer) || virtual_size > 0x10000 ||
      physical_size == 0 || physical_size > 0x100000) {
    RGPU_ERROR("XGRegister{}Shader {:08X}: no shader container at {:08X} (flags {:08X})",
               pixel ? "Pixel" : "Vertex", shader_va, container_va, flags);
    return;
  }
  std::vector<uint8_t> container(size_t(virtual_size) + physical_size);
  std::memcpy(container.data(), header, virtual_size);
  std::memcpy(container.data() + virtual_size, physical, physical_size);
  const uint64_t hash = HashShaderContainer(container.data());
  const RecordKind kind = pixel ? RecordKind::PixelShader : RecordKind::VertexShader;
  // Captured here, where its container is at hand, only when the cache lacks it.
  const ShaderCacheEntry *const sort_entry = FindShaderEntry(hash);
  if (!sort_entry)
    CaptureMissingShader(container.data(), pixel, hash);
  NoteShaderSortKey(shader_va, hash, ShaderSortableByHash(hash, sort_entry, pixel));  // banjo-nx (2026-10-27)

  // In order with the draws that use it (render thread).
  render_thread::Queue([shader_va, pixel, hash, kind] {
    auto &shader = g_guest_shaders[shader_va];
    if (shader && shader->hash == hash && shader->kind == kind)
      return;
    // A different shader in the same memory: pipelines keep their own compiled
    // copy of the old one, so its module can go now. Draws before it may also
    // have found none there (FindShaderLocked).
    ForgetShader(shader_va);
    if (shader) {  // banjo-nx (2026-10-23, batch 6): the old one's host shader kept
      std::lock_guard shader_lock(shader->host_mutex);
      ReleaseHostShader(shader.get());  // banjo-nx (2026-10-28)
    }
    shader = std::make_unique<ShaderRecord>();
    shader->kind = kind;
    shader->self_va = shader_va;
    shader->hash = hash;
    shader->entry = FindShaderEntry(hash);
    PipelineShaderLoaded(shader->entry);
  });
}

// nights-nx (from bejeweled3-nx, 2026-10-22): void D3DPixelShader_GetFunction(D3DPixelShader*, void *pData,
// UINT *pSizeOfData) (NiGHTS sub_82351100) and D3DVertexShader_GetFunction (sub_82351158): word for word Zuma's
// Revenge's sub_823142B0 / sub_82314308. Asked for the size first (pData NULL), then the bytes: the container's virtual
// part (+0x28 / +0x368, its size at +4) and the physical part (the pointer at +0x18 / +0x20, its size at +8). A shader
// this renderer created is its record, which has none of those fields: NiGHTS's first Renut run copied from garbage
// (sub_82337880 wrote to the unmapped 0x49910070 from the title's startup). The record keeps its container
// (CreateShaderRecord) and these give it back; shaders the game set up in place (XGRegister*Shader) have the real
// fields, so the original handles them.
bool GetShaderFunction(PPCContext &ctx, uint8_t *base, RecordKind kind) {
  auto *shader = FindRecord<ShaderRecord>(ctx.r3.u32, kind);
  if (!shader)
    return false;
  const u32 data = ctx.r4.u32, size_out = ctx.r5.u32;
  const u32 size = u32(shader->function.size());
  if (!data) {
    if (size_out)
      *reinterpret_cast<be_u32 *>(base + size_out) = size;
  } else if (size) {
    std::memcpy(base + data, shader->function.data(), size);
  }
  return true;
}

// void XGRegisterVertexShader(D3DVertexShader *pShader, void *pPhysicalPart) [0x8264E7B8]
// As the original: the header (a vertex shader of one reference), and for
// each of its bind slots (two when the container's flag 0x20 is set) the
// vertex fetch instructions copied from the microcode into the slot.
void XGRegisterVertexShader_hook(u32 shader, u32 physical) {
  auto *bytes = GuestPtr<u8>(shader);
  if (!bytes)
    return;
  auto word = [bytes](u32 offset) -> be_u32 & {
    return *reinterpret_cast<be_u32 *>(bytes + offset);
  };
  std::memset(bytes, 0, kVertexShaderContainer);
  word(kVertexShaderPhysical) = physical;
  word(0x00) = 6u;            // Common: D3DRTYPE_VERTEXSHADER
  word(0x04) = 1u;            // ReferenceCount
  word(0x14) = 0xFFFF0000u;   // BaseFlush
  const u32 slots = (u32(word(kVertexShaderContainer)) & 0x20) ? 2 : 1;
  for (u32 slot = 0; slot < slots; ++slot) {
    const u32 bind = 0x28 + slot * 0x1A0;
    const u32 table = u32(word(0x380 + slot * 8));
    const u32 fetches = u32(word(table + 0x384));
    const u32 microcode = u32(word(kVertexShaderPhysical)) + u32(word(table + 0x368));
    const u32 list = table + 0x368 + (u32(word(table + 0x380)) + 9) * 4;
    for (u32 i = 0; i < fetches; ++i) {
      const u32 source = (u32(word(list + i * 4)) & 0xFFF) * 12 + microcode;
      const u32 destination = shader + bind + 0x1C + i * 12;
      if (destination != source) {
        u8 *to = GuestPtr<u8>(destination);
        const u8 *from = GuestPtr<const u8>(source);
        if (to && from)
          std::memmove(to, from, 12);
      }
    }
    word(bind) = 0xFFFFFFFFu;
  }
  RegisterGuestShader(shader, physical, false);
}

// void XGRegisterPixelShader(D3DPixelShader *pShader, void *pPhysicalPart) [0x8264E670]
void XGRegisterPixelShader_hook(u32 shader, u32 physical) {
  auto *bytes = GuestPtr<u8>(shader);
  if (!bytes)
    return;
  auto *words = reinterpret_cast<be_u32 *>(bytes);
  std::memset(bytes, 0, kPixelShaderContainer);
  words[0] = 7u;            // Common: D3DRTYPE_PIXELSHADER
  words[1] = 1u;            // ReferenceCount
  words[5] = 0xFFFF0000u;   // BaseFlush
  words[6] = physical;      // the physical part (+0x18)
  RegisterGuestShader(shader, physical, true);
}

// D3DVertexShader *D3DDevice_CreateVertexShader(const DWORD *pFunction) [0x8264E8B0]
u32 D3DDevice_CreateVertexShader_hook(mapped_u32 function) {
  return CreateShaderRecord(static_cast<const be_u32 *>(function), false);
}

// D3DPixelShader *D3DDevice_CreatePixelShader(const DWORD *pFunction) [0x8264E6A8]
u32 D3DDevice_CreatePixelShader_hook(mapped_u32 function) {
  return CreateShaderRecord(static_cast<const be_u32 *>(function), true);
}

// D3DVertexDeclaration *D3DDevice_CreateVertexDeclaration(
//     const D3DVERTEXELEMENT9 *pVertexElements) [0x8264EA90]
u32 D3DDevice_CreateVertexDeclaration_hook(rex::MappedPtr<GuestVertexElement> elements) {
  if (!elements)
    return 0;
  auto *declaration = CreateRecord<VertexDeclarationRecord>(RecordKind::VertexDeclaration);
  if (!declaration)
    return 0;
  declaration->x360.resource.Common =
      kCommonD3DCreated | u32(d3d::ResourceType::VertexDeclaration);
  declaration->x360.resource.ReferenceCount = 1u;

  const GuestVertexElement *element = elements;
  for (u32 i = 0; i < 64 && u32(element[i].Type) != kDeclTypeUnused; ++i)
    declaration->elements.push_back(element[i]);
  BuildDeclarationLayout(*declaration);
  return declaration->self_va;
}

// void D3DVertexShader_Bind(D3DVertexShader *pShader, DWORD Flags,
//     D3DVertexDeclaration *pDecl, const DWORD *pStreamStrides, DWORD Index) [0x82657048]
// Patches a CVertexShader's vertex fetches to a declaration and the strides of
// streams 0..(highest stream in the declaration). Draws take their input layout
// from a declaration record instead, so nothing is patched; the declaration and
// strides are kept for draws that rely on the bind: N&B binds its shaders when
// it creates them and then draws with no declaration set, or with a zero
// SetStreamSource stride.
void D3DVertexShader_Bind_hook(u32 shader_va, u32 /*flags*/, u32 declaration_va,
                               mapped_u32 stream_strides, u32 /*index*/) {
  // The declaration and strides as they are now; the shader record is the
  // render thread's (draws read it), so it is updated there, in order.
  const auto *declaration =
      FindRecord<VertexDeclarationRecord>(declaration_va, RecordKind::VertexDeclaration);
  if (!declaration)
    return;
  auto bound = std::make_shared<VertexDeclarationRecord>(*declaration);
  std::vector<std::pair<u32, u32>> strides;  // stream, stride
  if (stream_strides) {
    const auto *guest_strides = static_cast<const be_u32 *>(stream_strides);
    for (const GuestVertexElement &element : declaration->elements) {
      const u32 stream = element.Stream;
      if (stream < d3d::kMaxStreams)
        strides.emplace_back(stream, u32(guest_strides[stream]));
    }
  }
  render_thread::Queue([shader_va, bound, strides = std::move(strides)] {
    auto *shader = FindShaderLocked(shader_va, RecordKind::VertexShader);
    if (!shader)
      return;
    // banjo-nx (2026-10-27, render thread night): the fast path and its stream memo may hold the declaration this
    // replaces (freed here) and read these strides: dropped (a bind after the first draws; N&B binds at creation).
    InvalidateFastDrawForRecordLocked(shader);  // banjo-nx (2026-10-28): its declaration goes (held via the shader)
    shader->bound_declaration = std::make_unique<VertexDeclarationRecord>(*bound);
    for (const auto &[stream, stride] : strides)
      shader->bound_strides[stream] = stride;
  });
}

} // namespace

BANJO_RENUT_HOOK(sub_82351398, D3DDevice_CreateVertexShader_hook);  // D3DDevice_CreateVertexShader  // N&B sub_8264E8B0
BANJO_RENUT_HOOK(sub_823511B0, D3DDevice_CreatePixelShader_hook);  // D3DDevice_CreatePixelShader  // N&B sub_8264E6A8
BANJO_RENUT_HOOK(sub_82350E28, D3DDevice_CreateVertexDeclaration_hook);  // D3DDevice_CreateVertexDeclaration  // N&B sub_8264EA90
#if 0  // nights-nx: N&B sub_82657048 (D3DVertexShader_Bind): not found in NiGHTS (not linked in Peggle 2 / Zuma's Revenge, no unique N&B shape)
BANJO_RENUT_HOOK(sub_82657048, D3DVertexShader_Bind_hook);  // D3DVertexShader_Bind
#endif
BANJO_RENUT_HOOK(sub_823512A0, XGRegisterVertexShader_hook);  // XGRegisterVertexShader  // N&B sub_8264E7B8
BANJO_RENUT_HOOK(sub_823508C8, XGRegisterPixelShader_hook);  // XGRegisterPixelShader  // N&B sub_8264E670
// nights-nx (from bejeweled3-nx): the shaders' GetFunction (GetShaderFunction above); not hooked in banjo-nx's tree.
BANJO_RENUT_HOOK_RAW(sub_82351100) {  // D3DPixelShader_GetFunction
  if (!GetShaderFunction(ctx, base, renut::gpu::RecordKind::PixelShader))
    __imp__sub_82351100(ctx, base);
}
BANJO_RENUT_HOOK_RAW(sub_82351158) {  // D3DVertexShader_GetFunction
  if (!GetShaderFunction(ctx, base, renut::gpu::RecordKind::VertexShader))
    __imp__sub_82351158(ctx, base);
}
