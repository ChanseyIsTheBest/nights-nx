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

#pragma once

#include <memory>
#include <string>

#include <rex/filesystem.h>
#include <rex/filesystem/entry.h>

namespace rex::filesystem {

class HostPathDevice;

class HostPathEntry : public Entry {
 public:
  HostPathEntry(Device* device, Entry* parent, const std::string_view path,
                const std::filesystem::path& host_path);
  ~HostPathEntry() override;

  // nights-nx (2026-11-22, culdcept-nx's): `packed_hint` from the VFS index: kPackedUnknown (check the name: a plain
  // NAME beside it, the header), kPackedNot, or the size of the file a NAME.lz4b (or NAME.adxo) stands for (no SD
  // access: the index knows it).
  static constexpr int64_t kPackedUnknown = -2, kPackedNot = -1;
  static HostPathEntry* Create(Device* device, Entry* parent,
                               const std::filesystem::path& full_path,
                               rex::filesystem::FileInfo file_info, int64_t packed_hint = kPackedUnknown);

  const std::filesystem::path& host_path() const { return host_path_; }
  // nights-nx (2026-11-22, rr6-nx's): host_path_ is an LZ4 / ZXC block file (NAME.lz4b, rex/filesystem/lz4_blocks.h)
  // shown as NAME with the size of the file it holds; opened through OpenLz4Blocks, never mapped or written.
  bool lz4_blocks() const { return lz4_blocks_; }
  // nights-nx (2026-11-22, sa2-nx-ult's): host_path_ is a NAME.adxo (rex/filesystem/adx_opus.h: a file of ADX streams
  // whose sound is Opus), shown as NAME with that file's size; opened through OpenAdxOpus, never mapped or written.
  bool adx_opus() const { return adx_opus_; }
  // Either: the guest's file is made from the host's one (read only, not mapped, its size the one it stands for).
  bool packed() const { return lz4_blocks_ || adx_opus_; }
  // culdcept-nx (2026-11-20): the size of the file on the SD (a NAME.lz4b's own size, not the size it holds), which the
  // VFS index compares with the SD's listing.
  uint64_t disk_size() const { return disk_size_; }

  /*
   * "All the children of this directory are already known."
   *
   * HostPathDevice::Initialize walks the game's whole tree and creates an entry for every file. Once
   * that has happened, GetChild failing is the final answer: the file does not exist. Without this flag
   * the device went to the SD to check again (a stat and, if that failed, a full enumeration of the
   * directory with one stat per entry), which cost ~32 ms per failed open. See host_path_device.cpp.
   */
  bool children_complete() const { return children_complete_; }

  // Only for rebuilding the tree from the on-disk index (host_path_index.cpp).
  void AddChildIndex(HostPathEntry* child) { children_.push_back(std::unique_ptr<Entry>(child)); }
  void MarkCompleteIndex(bool complete) { children_complete_ = complete; }
  void DiscardChildrenIndex() {
    children_.clear();
    children_complete_ = false;
  }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;
  bool Truncate() override;

  /*
   * culdcept-nx (2026-11-20, filesystem_keep_open): the host handle of a read-only file on a read-only
   * device, kept open after the guest closes it so the next open of the same file reuses it. On the
   * Switch an open is three file-system requests (open, the timestamp query in fsdev_open, the close),
   * 4-5 ms each time and over 100 ms at worst; Culdcept Saga reopens DATA00.RSP and its layouts and
   * sound banks every few seconds on the board. The handle is stateless (pread at an offset), so a
   * reused one reads exactly as a fresh one. Park gives the handle up (closed) when the key is off.
   */
  std::unique_ptr<rex::filesystem::FileHandle> TakeParked();
  void Park(std::unique_ptr<rex::filesystem::FileHandle> handle);

  bool can_map() const override { return !packed(); }  // nights-nx: any packed file (lz4b, adxo)
  std::unique_ptr<memory::MappedMemory> OpenMapped(memory::MappedMemory::Mode mode, size_t offset,
                                                   size_t length) override;
  void update() override;
  bool SetAttributes(uint64_t attributes) override;
  bool SetCreateTimestamp(uint64_t timestamp) override;
  bool SetAccessTimestamp(uint64_t timestamp) override;
  bool SetWriteTimestamp(uint64_t timestamp) override;

 private:
  friend class HostPathDevice;

  std::unique_ptr<Entry> CreateEntryInternal(const std::string_view name,
                                             uint32_t attributes) override;
  bool DeleteEntryInternal(Entry* entry) override;
  X_STATUS RenameEntryInternal(const std::vector<std::string_view>& path_parts) override;

  std::filesystem::path host_path_;
  bool children_complete_ = false;
  bool lz4_blocks_ = false;
  bool adx_opus_ = false;
  uint64_t disk_size_ = 0;
};

}  // namespace rex::filesystem
