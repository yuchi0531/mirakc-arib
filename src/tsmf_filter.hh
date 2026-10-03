// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <tsduck/tsduck.h>

#include "base.hh"
#include "logging.hh"
#include "packet_sink.hh"

#define MIRAKC_ARIB_TSMF_FILTER_TRACE(...) MIRAKC_ARIB_TRACE("tsmf-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_TSMF_FILTER_DEBUG(...) MIRAKC_ARIB_DEBUG("tsmf-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_TSMF_FILTER_INFO(...) MIRAKC_ARIB_INFO("tsmf-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_TSMF_FILTER_WARN(...) MIRAKC_ARIB_WARN("tsmf-filter: " __VA_ARGS__)
#define MIRAKC_ARIB_TSMF_FILTER_ERROR(...) MIRAKC_ARIB_ERROR("tsmf-filter: " __VA_ARGS__)

namespace {

struct TsmfFilterOption final {
  int relative_ts_number = 0;
};

// TSMF (MPEG-TS Multi Frame) filter
//
// TSMF multiplexes up to 15 relative TS streams into a single MPEG-TS stream
// for CATV retransmission.  Frame synchronization packets on PID 0x002F hold a
// table which maps each of the 52 slots to a relative TS number.  This filter
// outputs only the packets belonging to a specified relative TS stream.
//
// The behavior is compatible with the `tsmfRelTs` property of Mirakurun.
class TsmfFilter final : public PacketSink {
 public:
  explicit TsmfFilter(const TsmfFilterOption& option) : option_(option) {}

  virtual ~TsmfFilter() override {}

  void Connect(std::unique_ptr<PacketSink>&& sink) {
    sink_ = std::move(sink);
  }

  bool Start() override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    MIRAKC_ARIB_ASSERT(option_.relative_ts_number >= 1 && option_.relative_ts_number <= 15);
    return sink_->Start();
  }

  void End() override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    sink_->End();
  }

  int GetExitCode() const override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    return sink_->GetExitCode();
  }

  bool HandlePacket(const ts::TSPacket& packet) override {
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);

    auto pid = packet.getPID();

    if (pid == kPID_TSMF) {
      // The frame synchronization word occupies the 13 low-order bits of the
      // two bytes at offset 4.
      const uint16_t sync_word =
          static_cast<uint16_t>((packet.b[4] << 8) | packet.b[5]) & 0x1FFF;
      if (sync_word != kSyncWord1 && sync_word != kSyncWord2) {
        // Not a TSMF frame synchronization packet.
        return true;
      }

      // The 26-byte table at offset 73 holds the relative TS number of each of
      // the 52 slots as 4-bit values (two slots per byte).
      for (size_t i = 0; i < kNumSlots; i++) {
        relative_ts_numbers_[i] =
            (packet.b[kSlotTableOffset + i / 2] >> (i % 2 == 0 ? 4 : 0)) & 0x0F;
      }
      slot_counter_ = 0;
      return true;
    }

    // Before the first frame synchronization packet, or outside the 52 slots,
    // the relative TS number is unknown.  Drop such packets.
    if (slot_counter_ < 0 || slot_counter_ >= static_cast<int>(kNumSlots)) {
      return true;
    }

    auto slot = static_cast<size_t>(slot_counter_);
    slot_counter_++;

    if (relative_ts_numbers_[slot] != option_.relative_ts_number) {
      return true;
    }

    if (pid == ts::PID_NULL) {
      return true;
    }

    return sink_->HandlePacket(packet);
  }

 private:
  // PID carrying TSMF frame synchronization packets.
  static constexpr ts::PID kPID_TSMF = 0x002F;
  // Frame synchronization words.
  static constexpr uint16_t kSyncWord1 = 0x1A86;
  static constexpr uint16_t kSyncWord2 = 0x0579;
  // The number of slots in a TSMF frame.
  static constexpr size_t kNumSlots = 52;
  // Offset of the relative TS number table.
  static constexpr size_t kSlotTableOffset = 73;

  TsmfFilterOption option_;
  std::unique_ptr<PacketSink> sink_;
  std::array<uint8_t, kNumSlots> relative_ts_numbers_{};
  int slot_counter_ = -1;

  MIRAKC_ARIB_NON_COPYABLE(TsmfFilter);
};

}  // namespace
