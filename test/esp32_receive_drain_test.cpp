#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "roo_io/memory/memory_output_stream.h"
#include "roo_io/memory/store.h"
#include "roo_transport/link/arduino/esp32/reliable_serial.h"
#include "roo_transport/link/internal/protocol.h"

namespace roo_transport {
namespace {

/// Models a serial stream whose receive notifications can be coalesced.
class CoalescedSerial : public Stream {
 public:
  /// Reports queued data until the optional disappearing-data check fires.
  int available() override {
    ++availability_checks_;
    if (hide_after_snapshot_ && availability_checks_ > 1) return 0;
    return static_cast<int>(bytes_.size());
  }

  /// Reads one byte, optionally replacing it with newly arriving traffic.
  int read() override {
    if (bytes_.empty()) return -1;
    const uint8_t result = bytes_.front();
    bytes_.pop_front();
    ++bytes_read_;
    if (refill_remaining_ > 0) {
      bytes_.push_back(0x55);
      --refill_remaining_;
    }
    return result;
  }

  /// Returns the next queued byte without consuming it.
  int peek() override { return bytes_.empty() ? -1 : bytes_.front(); }

  /// Discards transport output; these tests drive only the input side.
  size_t write(uint8_t) override { return 1; }

  /// Discards a complete write without blocking the idle sender worker.
  size_t write(const uint8_t*, size_t count) override { return count; }

  /// Reports enough immediate write capacity for a complete framed packet.
  int availableForWrite() override { return 256; }

  /// Stores the receive callback without generating any automatic event.
  void onReceive(std::function<void()> fn) { receive_ = std::move(fn); }

  /// Stores the error callback without generating any automatic event.
  void onReceiveError(std::function<void(hardwareSerial_error_t)> fn) {
    error_ = std::move(fn);
  }

  /// Queues bytes without notifying, so one event can represent a large batch.
  void append(const uint8_t* bytes, size_t count) {
    bytes_.insert(bytes_.end(), bytes, bytes + count);
  }

  /// Queues delimiter-free data that does not produce a valid packet.
  void appendUnframed(size_t count) {
    bytes_.insert(bytes_.end(), count, 0x55);
  }

  /// Makes subsequent reads bring in at most @p count fresh bytes.
  void replenishOnRead(size_t count) { refill_remaining_ = count; }

  /// Models data disappearing between the callback snapshot and its first read.
  void hideAfterSnapshot() { hide_after_snapshot_ = true; }

  /// Delivers exactly one receive notification.
  void notifyReceive() {
    if (receive_ != nullptr) receive_();
  }

  /// Delivers exactly one receive-error notification.
  void notifyError() {
    if (error_ != nullptr) error_(UART_FIFO_OVF_ERROR);
  }

  size_t unreadBytes() const { return bytes_.size(); }
  size_t bytesRead() const { return bytes_read_; }
  size_t availabilityChecks() const { return availability_checks_; }

 private:
  std::deque<uint8_t> bytes_;
  std::function<void()> receive_;
  std::function<void(hardwareSerial_error_t)> error_;
  size_t bytes_read_ = 0;
  size_t availability_checks_ = 0;
  size_t refill_remaining_ = 0;
  bool hide_after_snapshot_ = false;
};

class Esp32ReceiveDrainTest : public testing::Test {
 protected:
  Esp32ReceiveDrainTest() : transport(serial, UART_NUM_1, "drain") {
    transport.begin();
  }

  ~Esp32ReceiveDrainTest() override { transport.end(); }

  CoalescedSerial serial;
  esp32::Esp32SerialLinkTransport<CoalescedSerial> transport;
};

// Verifies that a single event drains more than one chunk even when every chunk
// is a partial/malformed frame and therefore delivers zero valid packets.
TEST_F(Esp32ReceiveDrainTest, DrainsEntryBacklogWithoutValidPackets) {
  serial.appendUnframed(800);
  serial.notifyReceive();
  EXPECT_EQ(serial.bytesRead(), 800u);
  EXPECT_EQ(serial.unreadBytes(), 0u);
}

// Verifies that receive errors use the same draining path, so buffered data
// does not depend on another data event arriving after the error notification.
TEST_F(Esp32ReceiveDrainTest, ErrorNotificationDrainsEntryBacklog) {
  serial.appendUnframed(800);
  serial.notifyError();
  EXPECT_EQ(serial.bytesRead(), 800u);
  EXPECT_EQ(serial.unreadBytes(), 0u);
}

// Verifies that fresh input cannot make a high-priority callback run forever.
// The finite replenishment budget also lets an unbounded implementation finish
// and fail the byte-count assertion rather than hanging the test process.
TEST_F(Esp32ReceiveDrainTest, LeavesFreshArrivalsForLaterCallbacks) {
  serial.appendUnframed(768);
  serial.replenishOnRead(2048);
  serial.notifyReceive();
  EXPECT_EQ(serial.bytesRead(), 768u);
  EXPECT_EQ(serial.unreadBytes(), 768u);
}

// Verifies that an empty read ends the callback even when the initial snapshot
// reported bytes. The adapter rechecks availability before attempting a read.
TEST_F(Esp32ReceiveDrainTest, StopsWhenSnapshotNoLongerReadable) {
  serial.appendUnframed(800);
  serial.hideAfterSnapshot();
  serial.notifyReceive();
  EXPECT_EQ(serial.bytesRead(), 0u);
  EXPECT_EQ(serial.availabilityChecks(), 2u);
  EXPECT_EQ(serial.unreadBytes(), 800u);
}

// Verifies that one notification delivers several complete, validated frames
// through the reliable receiver, rather than merely discarding raw input.
TEST_F(Esp32ReceiveDrainTest, DeliversAllPacketsFromOneNotification) {
  Link link = transport.transport().connectAsync();
  const uint32_t my_id = link.streamId();
  const uint32_t peer_id = my_id == 1 ? 2 : 1;
  constexpr uint16_t kFirstSequence = 100;
  std::array<roo::byte, 11> handshake{};
  roo_io::StoreBeU16(
      internal::FormatPacketHeader(internal::SeqNum(kFirstSequence),
                                   internal::kHandshakePacket, false),
      handshake.data());
  roo_io::StoreBeU32(peer_id, handshake.data() + 2);
  roo_io::StoreBeU32(my_id, handshake.data() + 6);
  handshake[10] = static_cast<roo::byte>(kBufferSize4KB);
  transport.transport().processIncomingPacket(handshake.data(),
                                              handshake.size());
  ASSERT_EQ(link.status(), LinkStatus::kConnected);

  std::array<roo::byte, 4 * 256> frames{};
  roo_io::MemoryOutputStream<roo::byte*> frame_output(
      frames.data(), frames.data() + frames.size());
  PacketSenderOverStream frame_sender(frame_output);
  std::vector<roo::byte> expected;
  for (uint16_t index = 0; index < 4; ++index) {
    std::array<roo::byte, 250> packet{};
    roo_io::StoreBeU16(
        internal::FormatPacketHeader(internal::SeqNum(kFirstSequence + index),
                                     internal::kDataPacket, peer_id > my_id),
        packet.data());
    for (size_t i = 2; i < packet.size(); ++i) {
      packet[i] = static_cast<roo::byte>((index * 73 + i) & 0xff);
    }
    expected.insert(expected.end(), packet.begin() + 2, packet.end());
    frame_sender.send(packet.data(), packet.size());
  }
  ASSERT_EQ(frame_output.status(), roo_io::kOk);
  serial.append(reinterpret_cast<const uint8_t*>(frames.data()),
                frame_output.ptr() - frames.data());
  serial.notifyReceive();

  std::vector<roo::byte> received(expected.size());
  EXPECT_EQ(link.in().tryRead(received.data(), received.size()),
            expected.size());
  EXPECT_EQ(received, expected);
  EXPECT_EQ(transport.statsMonitor().packets_received(), 4u);
  EXPECT_EQ(serial.unreadBytes(), 0u);
  link.disconnect();
}

}  // namespace
}  // namespace roo_transport
