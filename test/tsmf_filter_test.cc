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

#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tsduck/tsduck.h>

#include "tsmf_filter.hh"

#include "test_helper.hh"

namespace {

constexpr ts::PID kTsmfPID = 0x002F;
constexpr size_t kNumSlots = 52;
constexpr size_t kSlotTableOffset = 73;
constexpr ts::PID kFirstPayloadPID = 0x0100;

using RelativeTsTable = std::array<uint8_t, kNumSlots>;

// Build a TSMF frame synchronization packet (PID 0x002F) carrying the given
// sync word and relative TS table.
ts::TSPacket MakeTsmfSyncPacket(uint16_t sync_word, const RelativeTsTable& table) {
  ts::TSPacket packet;
  packet.init(kTsmfPID, 0);
  packet.b[4] = static_cast<uint8_t>((sync_word >> 8) & 0xFF);
  packet.b[5] = static_cast<uint8_t>(sync_word & 0xFF);
  for (size_t i = 0; i < kNumSlots; i++) {
    auto nibble = static_cast<uint8_t>(table[i] & 0x0F);
    if (i % 2 == 0) {
      packet.b[kSlotTableOffset + i / 2] =
          static_cast<uint8_t>((packet.b[kSlotTableOffset + i / 2] & 0x0F) | (nibble << 4));
    } else {
      packet.b[kSlotTableOffset + i / 2] =
          static_cast<uint8_t>((packet.b[kSlotTableOffset + i / 2] & 0xF0) | nibble);
    }
  }
  return packet;
}

ts::TSPacket MakePayloadPacket(ts::PID pid) {
  ts::TSPacket packet;
  packet.init(pid, 0);
  return packet;
}

// Make the source return `packets` in order and then signal EOF.
void SetUpSource(MockSource* src, std::vector<ts::TSPacket> packets) {
  auto packets_ptr = std::make_shared<std::vector<ts::TSPacket>>(std::move(packets));
  auto index = std::make_shared<size_t>(0);
  EXPECT_CALL(*src, GetNextPacket(testing::_))
      .WillRepeatedly([packets_ptr, index](ts::TSPacket* packet) {
        if (*index >= packets_ptr->size()) {
          return false;
        }
        ts::TSPacket::Copy(packet, &(*packets_ptr)[(*index)++]);
        return true;
      });
}

}  // namespace

TEST(TsmfFilterTest, ForwardRequestedRelativeTs) {
  // Slot i belongs to relative TS number (i % 3) + 1.
  RelativeTsTable table;
  for (size_t i = 0; i < kNumSlots; i++) {
    table[i] = static_cast<uint8_t>((i % 3) + 1);
  }

  const int requested = 2;

  std::vector<ts::TSPacket> packets;
  std::vector<ts::PID> expected;
  packets.push_back(MakeTsmfSyncPacket(0x1A86, table));
  for (size_t i = 0; i < kNumSlots; i++) {
    auto pid = static_cast<ts::PID>(kFirstPayloadPID + i);
    packets.push_back(MakePayloadPacket(pid));
    if (table[i] == requested) {
      expected.push_back(pid);
    }
  }

  TsmfFilterOption option;
  option.relative_ts_number = requested;
  auto filter = std::make_unique<TsmfFilter>(option);
  auto sink = std::make_unique<MockSink>();
  MockSource src;

  SetUpSource(&src, std::move(packets));

  std::vector<ts::PID> forwarded;
  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket)
        .Times(static_cast<int>(expected.size()))
        .WillRepeatedly([&forwarded](const ts::TSPacket& packet) {
          forwarded.push_back(packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(expected, forwarded);
}

TEST(TsmfFilterTest, AcceptSecondSyncWord) {
  RelativeTsTable table;
  for (size_t i = 0; i < kNumSlots; i++) {
    table[i] = static_cast<uint8_t>((i % 3) + 1);
  }

  const int requested = 1;

  std::vector<ts::TSPacket> packets;
  std::vector<ts::PID> expected;
  packets.push_back(MakeTsmfSyncPacket(0x0579, table));
  for (size_t i = 0; i < kNumSlots; i++) {
    auto pid = static_cast<ts::PID>(kFirstPayloadPID + i);
    packets.push_back(MakePayloadPacket(pid));
    if (table[i] == requested) {
      expected.push_back(pid);
    }
  }

  TsmfFilterOption option;
  option.relative_ts_number = requested;
  auto filter = std::make_unique<TsmfFilter>(option);
  auto sink = std::make_unique<MockSink>();
  MockSource src;

  SetUpSource(&src, std::move(packets));

  std::vector<ts::PID> forwarded;
  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket)
        .Times(static_cast<int>(expected.size()))
        .WillRepeatedly([&forwarded](const ts::TSPacket& packet) {
          forwarded.push_back(packet.getPID());
          return true;
        });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ(expected, forwarded);
}

TEST(TsmfFilterTest, MaskSyncWordBits) {
  RelativeTsTable table;
  for (size_t i = 0; i < kNumSlots; i++) {
    table[i] = 1;
  }

  std::vector<ts::TSPacket> packets;
  // Set the three high-order bits of the sync word.  Only the 13 low-order
  // bits (0x1A86) must be considered.
  auto sync = MakeTsmfSyncPacket(0x1A86, table);
  sync.b[4] |= 0xE0;
  EXPECT_EQ(0x1A86, (((sync.b[4] << 8) | sync.b[5]) & 0x1FFF));
  packets.push_back(sync);
  packets.push_back(MakePayloadPacket(kFirstPayloadPID));

  TsmfFilterOption option;
  option.relative_ts_number = 1;
  auto filter = std::make_unique<TsmfFilter>(option);
  auto sink = std::make_unique<MockSink>();
  MockSource src;

  SetUpSource(&src, std::move(packets));

  std::vector<ts::PID> forwarded;
  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([&forwarded](const ts::TSPacket& packet) {
      forwarded.push_back(packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ((std::vector<ts::PID>{kFirstPayloadPID}), forwarded);
}

TEST(TsmfFilterTest, DropPacketsBeforeValidSyncWord) {
  RelativeTsTable table;
  for (size_t i = 0; i < kNumSlots; i++) {
    table[i] = 1;
  }

  std::vector<ts::TSPacket> packets;
  // Packets before the first valid frame synchronization packet are dropped.
  packets.push_back(MakePayloadPacket(kFirstPayloadPID));
  // A frame synchronization packet with a wrong sync word is ignored and must
  // not initialize the slot counter.
  packets.push_back(MakeTsmfSyncPacket(0x0000, table));
  packets.push_back(MakePayloadPacket(static_cast<ts::PID>(kFirstPayloadPID + 1)));
  // The first valid frame synchronization packet initializes the slot counter.
  packets.push_back(MakeTsmfSyncPacket(0x1A86, table));
  packets.push_back(MakePayloadPacket(static_cast<ts::PID>(kFirstPayloadPID + 2)));

  TsmfFilterOption option;
  option.relative_ts_number = 1;
  auto filter = std::make_unique<TsmfFilter>(option);
  auto sink = std::make_unique<MockSink>();
  MockSource src;

  SetUpSource(&src, std::move(packets));

  std::vector<ts::PID> forwarded;
  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([&forwarded](const ts::TSPacket& packet) {
      forwarded.push_back(packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ((std::vector<ts::PID>{static_cast<ts::PID>(kFirstPayloadPID + 2)}), forwarded);
}

TEST(TsmfFilterTest, DropNullPacket) {
  RelativeTsTable table;
  for (size_t i = 0; i < kNumSlots; i++) {
    table[i] = 1;
  }

  std::vector<ts::TSPacket> packets;
  packets.push_back(MakeTsmfSyncPacket(0x1A86, table));
  packets.push_back(MakePayloadPacket(ts::PID_NULL));
  packets.push_back(MakePayloadPacket(kFirstPayloadPID));

  TsmfFilterOption option;
  option.relative_ts_number = 1;
  auto filter = std::make_unique<TsmfFilter>(option);
  auto sink = std::make_unique<MockSink>();
  MockSource src;

  SetUpSource(&src, std::move(packets));

  std::vector<ts::PID> forwarded;
  {
    testing::InSequence seq;
    EXPECT_CALL(*sink, Start).WillOnce(testing::Return(true));
    EXPECT_CALL(*sink, HandlePacket).WillOnce([&forwarded](const ts::TSPacket& packet) {
      forwarded.push_back(packet.getPID());
      return true;
    });
    EXPECT_CALL(*sink, End).WillOnce(testing::Return());
    EXPECT_CALL(*sink, GetExitCode).WillOnce(testing::Return(EXIT_SUCCESS));
  }

  filter->Connect(std::move(sink));
  src.Connect(std::move(filter));
  EXPECT_EQ(EXIT_SUCCESS, src.FeedPackets());
  EXPECT_EQ((std::vector<ts::PID>{kFirstPayloadPID}), forwarded);
}
