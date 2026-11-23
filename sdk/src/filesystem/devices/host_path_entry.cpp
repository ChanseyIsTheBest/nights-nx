/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/devices/host_path_file.h>
#include "startup_trace.h"

#include <rex/filesystem.h>
#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/adx_opus.h>    // nights-nx (2026-11-22, sa2-nx-ult's)
#include <rex/filesystem/lz4_blocks.h>  // nights-nx (2026-11-22, rr6-nx's)
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/mapped_memory.h>
#include <rex/string.h>
#include <rex/cvar.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <mutex>

/*
 * culdcept-nx (2026-11-20): keep the game's data files open (HostPathEntry::Park). The console profile of a board
 * replay (out/console_runs/prof1) had 18 % of the game thread's slow frames in file-system requests: Culdcept Saga
 * reopens DATA00.RSP, its layout*.RSL and se_*.xma banks every few seconds, and each open on Horizon is _fsFsOpenCommon
 * plus fsFsGetFileTimeStampRaw (inside fsdev_open) and each close _fsObjectClose, 4-5 ms an open on average, the worst
 * over 100 ms. Only read-only opens of a read-only device are kept (the game data, never a save). A handle is just a
 * descriptor read with pread at an offset, so a reused one returns the same bytes as a fresh one.
 */
REXCVAR_DEFINE_BOOL(filesystem_keep_open, true, "Filesystem",
                    "Keep the host handles of read-only game files open after the guest closes them and reuse "
                    "them on the next open (no open, timestamp or close request to the file system).");
REXCVAR_DEFINE_INT32(filesystem_keep_open_max, 32, "Filesystem",
                     "How many closed read-only handles stay open at most (the oldest is closed beyond it).");

#if REX_PLATFORM_SWITCH
namespace rex::filesystem {
bool TruncateOpenFile(const std::filesystem::path& path);  // core/filesystem_posix.cpp (port 203)
}
#endif

namespace rex::filesystem {

namespace {

struct ParkedHandle {
  const HostPathEntry* entry;
  std::unique_ptr<FileHandle> handle;
};

// Never destroyed: a device torn down during static destruction at exit still finds them.
struct Parked {
  std::mutex mutex;
  std::list<ParkedHandle> list;  // newest first
};
Parked& P() {
  static Parked* p = new Parked();
  return *p;
}
std::atomic<uint64_t> g_keep_reused{0};
std::atomic<uint64_t> g_keep_parked{0};
std::atomic<uint64_t> g_keep_evicted{0};
std::atomic<uint64_t> g_keep_restats_skipped{0};
std::atomic<uint64_t> g_keep_retries{0};

// Closes every parked handle; how many there were.
size_t DropAllParked() {
  std::list<ParkedHandle> all;  // closed after the lock is let go
  {
    std::lock_guard lock(P().mutex);
    all.swap(P().list);
  }
  return all.size();
}

}  // namespace

StatsKeepOpen ReadStatsKeepOpen() {
  StatsKeepOpen s;
  s.reused = g_keep_reused.load(std::memory_order_relaxed);
  s.parked = g_keep_parked.load(std::memory_order_relaxed);
  s.evicted = g_keep_evicted.load(std::memory_order_relaxed);
  s.restats_skipped = g_keep_restats_skipped.load(std::memory_order_relaxed);
  std::lock_guard lock(P().mutex);
  s.live = P().list.size();
  return s;
}

void NoteRestatSkipped() {
  g_keep_restats_skipped.fetch_add(1, std::memory_order_relaxed);
}

std::unique_ptr<FileHandle> HostPathEntry::TakeParked() {
  std::lock_guard lock(P().mutex);
  for (auto it = P().list.begin(); it != P().list.end(); ++it) {
    if (it->entry == this) {
      auto handle = std::move(it->handle);
      P().list.erase(it);
      g_keep_reused.fetch_add(1, std::memory_order_relaxed);
      return handle;
    }
  }
  return nullptr;
}

void HostPathEntry::Park(std::unique_ptr<FileHandle> handle) {
  static const bool keep = REXCVAR_GET(filesystem_keep_open);
  static const size_t keep_max = size_t(std::max(0, int(REXCVAR_GET(filesystem_keep_open_max))));
  if (!handle || !keep || !keep_max) {
    return;  // closed here
  }
  std::unique_ptr<FileHandle> evicted;  // closed after the lock is let go (declared first, destroyed last)
  std::lock_guard lock(P().mutex);
  P().list.push_front(ParkedHandle{this, std::move(handle)});
  g_keep_parked.fetch_add(1, std::memory_order_relaxed);
  if (P().list.size() > keep_max) {
    evicted = std::move(P().list.back().handle);
    P().list.pop_back();
    g_keep_evicted.fetch_add(1, std::memory_order_relaxed);
  }
}

HostPathEntry::HostPathEntry(Device* device, Entry* parent, const std::string_view path,
                             const std::filesystem::path& host_path)
    : Entry(device, parent, path), host_path_(host_path) {}

HostPathEntry::~HostPathEntry() {
  std::list<ParkedHandle> mine;  // closed after the lock is let go (declared first, destroyed last)
  std::lock_guard lock(P().mutex);
  for (auto it = P().list.begin(); it != P().list.end();) {
    auto next = std::next(it);
    if (it->entry == this) {
      mine.splice(mine.end(), P().list, it);
    }
    it = next;
  }
}

HostPathEntry* HostPathEntry::Create(Device* device, Entry* parent,
                                     const std::filesystem::path& full_path,
                                     rex::filesystem::FileInfo file_info, int64_t packed_hint) {
  std::string name = rex::path_to_utf8(file_info.name);
  /*
   * nights-nx (2026-11-22, rr6-nx's of 2026-11-10 in culdcept-nx's form; the user: "help nights into dreams have l4z
   * and zxc in its web installer and rencode video and audio to opus and h265"): NAME.lz4b is the game's NAME
   * (rex/filesystem/lz4_blocks.h), with the size of the file it holds, unless a plain NAME lies next to it (an install
   * extracted over an older one): the plain file wins and the .lz4b stays an ordinary file the game never asks for.
   * A movie re-encoded to H.265 and packed (NAME.wmv.lz4b) is one of these: the ASF file it holds is the movie.
   * NAME.adxo (sa2-nx-ult's rex/filesystem/adx_opus.h: ADX streams whose sound is Opus) the same way.
   */
  bool lz4_blocks = false, adx_opus = false;
  uint64_t packed_size = 0;
  const uint64_t disk_size = file_info.total_size;
  const bool is_file = file_info.type == rex::filesystem::FileInfo::Type::kFile;
  const bool lz4_name = is_file && HasLz4BlocksSuffix(name), adxo_name = is_file && !lz4_name && HasAdxOpusSuffix(name);
  const size_t suffix = lz4_name ? kLz4BlocksSuffix.size() : kAdxOpusSuffix.size();
  if (packed_hint >= 0 && (lz4_name || adxo_name)) {
    // culdcept-nx (2026-11-20): from the VFS index, which made this choice in the full scan (and is thrown away when the
    // listing changes): no stat of the plain NAME and no read of the header, for each of a packed game's files.
    lz4_blocks = lz4_name;
    adx_opus = adxo_name;
    packed_size = uint64_t(packed_hint);
    name.resize(name.size() - suffix);
  } else if (packed_hint == kPackedUnknown && (lz4_name || adxo_name)) {
    std::filesystem::path plain = full_path;
    plain.replace_extension();
    rex::filesystem::FileInfo plain_info;
    if (rex::filesystem::GetInfo(plain, &plain_info)) {
      REXFS_WARN("{} and {} are both in the game folder: the plain file is used (delete one of them)",
                 rex::path_to_utf8(plain), rex::path_to_utf8(full_path));
    } else if (lz4_name ? ReadLz4BlocksSize(full_path, &packed_size) : ReadAdxOpusSize(full_path, &packed_size)) {
      lz4_blocks = lz4_name;
      adx_opus = adxo_name;
      name.resize(name.size() - suffix);
    } else {
      REXFS_WARN("{} is not a valid {} file: left as it is", rex::path_to_utf8(full_path),
                 lz4_name ? ".lz4b" : ".adxo");
    }
  }
  auto path = rex::string::utf8_join_guest_paths(parent->path(), name);
  auto entry = new HostPathEntry(device, parent, path, full_path);
  entry->lz4_blocks_ = lz4_blocks;
  entry->adx_opus_ = adx_opus;
  entry->disk_size_ = disk_size;
  if (lz4_blocks || adx_opus) {
    file_info.total_size = size_t(packed_size);
  }

  entry->create_timestamp_ = file_info.create_timestamp;
  entry->access_timestamp_ = file_info.access_timestamp;
  entry->write_timestamp_ = file_info.write_timestamp;
  if (file_info.type == rex::filesystem::FileInfo::Type::kDirectory) {
    entry->attributes_ = kFileAttributeDirectory;
  } else {
    entry->attributes_ = kFileAttributeNormal;
    if (device->is_read_only()) {
      entry->attributes_ |= kFileAttributeReadOnly;
    }
    entry->size_ = file_info.total_size;
    entry->allocation_size_ = rex::round_up(file_info.total_size, device->bytes_per_sector());
  }
  return entry;
}

X_STATUS HostPathEntry::Open(uint32_t desired_access, File** out_file) {
  if (is_read_only() &&
      (desired_access & (FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    REXFS_ERROR("Attempting to open file for write access on read-only device");
    return X_STATUS_ACCESS_DENIED;
  }
  /*
   * A folder is not opened as a file.
   *
   * On the Xbox 360, opening a directory is legal: games do it to enumerate it or just to check that
   * the drive is mounted. The game opens the root of its save container ("rmcsave:\") right after
   * mounting it, and this code used to try to open a FILE* on that folder. That always fails,
   * NO_SUCH_FILE was returned, and the game read it as "this save cannot be used": hence the
   * "damaged profile" on load. Creating a profile did not hit it, because then the game writes a
   * real file inside.
   *
   * A File without a handle is enough: what is done with a directory is enumerating it, and
   * XFile::QueryDirectory walks the entry tree without touching the disk. Reads and writes on it
   * are rejected further down.
   */
  if (attributes_ & kFileAttributeDirectory) {
    *out_file = new HostPathFile(desired_access, this, nullptr);
    return X_STATUS_SUCCESS;
  }

  // nights-nx (2026-11-22, rr6-nx's): an .lz4b or .adxo file is read only.
  if (packed() && (desired_access & (FileAccess::kGenericWrite | FileAccess::kFileWriteData |
                                        FileAccess::kFileAppendData | FileAccess::kGenericAll))) {
    return X_STATUS_ACCESS_DENIED;
  }

  // A read handle the startup preload thread opened ahead of the guest (read-only access only).
  std::unique_ptr<rex::filesystem::FileHandle> file_handle;
  if (is_read_only() && !(desired_access & (FileAccess::kGenericWrite | FileAccess::kFileWriteData |
                                            FileAccess::kFileAppendData | FileAccess::kGenericAll))) {
    // culdcept-nx (2026-11-20): or the one a previous open of this file left parked (already wrapped for an .lz4b).
    file_handle = TakeParked();
    if (file_handle) {
      *out_file = new HostPathFile(desired_access, this, std::move(file_handle));
      return X_STATUS_SUCCESS;
    }
    file_handle = startup_trace::TakePreopened(static_cast<HostPathDevice*>(device_), path());
  }
  if (!file_handle) {
    file_handle = rex::filesystem::FileHandle::OpenExisting(
        host_path_, desired_access, static_cast<HostPathDevice*>(device_)->allow_share_delete());
  }
  // culdcept-nx (2026-11-20, filesystem_keep_open): an open that fails while handles are parked gives them all up and is
  // tried once more, on any device (game data or saves): whatever limit the system puts on open files, the parked
  // handles never take a slot a real open needs.
  if (!file_handle) {
    if (const size_t dropped = DropAllParked()) {
      file_handle = rex::filesystem::FileHandle::OpenExisting(
          host_path_, desired_access, static_cast<HostPathDevice*>(device_)->allow_share_delete());
      if (g_keep_retries.fetch_add(1, std::memory_order_relaxed) < 20)
        REXFS_WARN("[io] keep-open: opening '{}' failed with {} handles parked; all closed, opened again: {}",
                   rex::path_to_utf8(host_path_), dropped, file_handle ? "opened" : "failed again");
    }
  }
  if (!file_handle) {
    // TODO(benvanik): pick correct response.
    return X_STATUS_NO_SUCH_FILE;
  }
  if (lz4_blocks_) {
    file_handle = OpenLz4Blocks(std::move(file_handle));  // logs why when it is not a valid .lz4b file
    if (!file_handle) {
      return X_STATUS_NO_SUCH_FILE;
    }
  } else if (adx_opus_) {
    file_handle = OpenAdxOpus(std::move(file_handle));  // logs why when it is not a valid .adxo file
    if (!file_handle) {
      return X_STATUS_NO_SUCH_FILE;
    }
  }
  *out_file = new HostPathFile(desired_access, this, std::move(file_handle));
  return X_STATUS_SUCCESS;
}

std::unique_ptr<memory::MappedMemory> HostPathEntry::OpenMapped(memory::MappedMemory::Mode mode,
                                                                size_t offset, size_t length) {
  if (packed()) {
    return nullptr;  // rr6-nx: the host file is the packed one
  }
  return memory::MappedMemory::Open(host_path_, mode, offset, length);
}

bool HostPathEntry::Truncate() {
  if (is_read_only() || (attributes_ & kFileAttributeDirectory)) {
    return false;
  }
#if REX_PLATFORM_SWITCH
  // Port 203 (sims3-nx's): a file that is already open is truncated through its one Horizon handle.
  if (rex::filesystem::TruncateOpenFile(host_path_)) {
    size_ = 0;
    allocation_size_ = 0;
    return true;
  }
#endif
  auto file = rex::filesystem::OpenFile(host_path_, "wb");
  if (!file) {
    return false;
  }
  fclose(file);
  size_ = 0;
  allocation_size_ = 0;
  return true;
}

std::unique_ptr<Entry> HostPathEntry::CreateEntryInternal(const std::string_view name,
                                                          uint32_t attributes) {
  auto full_path = host_path_ / rex::to_path(name);
  if (attributes & kFileAttributeDirectory) {
    if (!std::filesystem::create_directories(full_path)) {
      return nullptr;
    }
  } else {
    auto file = rex::filesystem::OpenFile(full_path, "wb");
    if (!file) {
      return nullptr;
    }
    fclose(file);
  }
  rex::filesystem::FileInfo file_info;
  if (!rex::filesystem::GetInfo(full_path, &file_info)) {
    return nullptr;
  }
  return std::unique_ptr<Entry>(HostPathEntry::Create(device_, this, full_path, file_info));
}

bool HostPathEntry::DeleteEntryInternal(Entry* entry) {
  auto full_path = host_path_ / rex::to_path(entry->name());
  std::error_code ec;  // avoid exception on remove/remove_all failure
  if (entry->attributes() & kFileAttributeDirectory) {
    // Delete entire directory and contents.
    auto removed = std::filesystem::remove_all(full_path, ec);
    return removed >= 1 && removed != static_cast<std::uintmax_t>(-1);
  } else {
    // Delete file.
    return !std::filesystem::is_directory(full_path) && std::filesystem::remove(full_path, ec);
  }
}

X_STATUS HostPathEntry::RenameEntryInternal(const std::vector<std::string_view>& path_parts) {
  auto new_host_path = static_cast<HostPathDevice*>(device_)->host_path();
  for (const auto& path_part : path_parts) {
    new_host_path /= rex::to_path(path_part);
  }

  std::error_code ec;
  std::filesystem::rename(host_path_, new_host_path, ec);
  if (ec) {
    REXFS_ERROR("RenameEntryInternal: failed to rename '{}' to '{}': {} ({})",
                rex::path_to_utf8(host_path_), rex::path_to_utf8(new_host_path), ec.message(),
                ec.value());
    return X_STATUS_ACCESS_DENIED;
  }

  host_path_ = new_host_path;
  return X_STATUS_SUCCESS;
}

void HostPathEntry::update() {
  rex::filesystem::FileInfo file_info;
  if (!rex::filesystem::GetInfo(host_path_, &file_info)) {
    return;
  }
  if (file_info.type == rex::filesystem::FileInfo::Type::kFile) {
    if (lz4_blocks_ || adx_opus_) {  // rr6-nx (sa2-nx-ult: and an .adxo): the size of the file it stands for
      uint64_t size = 0;
      if (!(lz4_blocks_ ? ReadLz4BlocksSize(host_path_, &size) : ReadAdxOpusSize(host_path_, &size))) {
        return;
      }
      file_info.total_size = size_t(size);
    }
    size_ = file_info.total_size;
    allocation_size_ = rex::round_up(file_info.total_size, device()->bytes_per_sector());
  }
}

bool HostPathEntry::SetAttributes(uint64_t attributes) {
  if (device_->is_read_only()) {
    return false;
  }
  attributes_ = static_cast<uint32_t>(attributes);
  return true;
}

bool HostPathEntry::SetCreateTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    return false;
  }
  create_timestamp_ = timestamp;
  return true;
}

bool HostPathEntry::SetAccessTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    return false;
  }
  access_timestamp_ = timestamp;
  return true;
}

bool HostPathEntry::SetWriteTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    return false;
  }
  write_timestamp_ = timestamp;
  return true;
}

}  // namespace rex::filesystem
