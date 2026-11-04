#include <algorithm>
#include <vector>

#include "gtest/gtest.h"
#include "roo_io/memory/load.h"
#include "roo_transport/link/internal/protocol.h"
#include "roo_transport/link/internal/thread_safe/channel.h"

namespace roo_transport {
namespace {

class QueuedPacketSender : public PacketSender {
 public:
  void send(const roo::byte* bytes, size_t count) override {
    packets.emplace_back(bytes, bytes + count);
  }

  void deliverTo(Channel& peer) {
    for (const auto& packet : packets) {
      peer.packetReceived(packet.data(), packet.size());
    }
    packets.clear();
  }

  size_t dataPackets() const {
    return std::count_if(
        packets.begin(), packets.end(), [](const auto& packet) {
          return internal::GetPacketType(roo_io::LoadBeU16(packet.data())) ==
                 internal::kDataPacket;
        });
  }

  std::vector<std::vector<roo::byte>> packets;
};

class ChannelSendBurstTest : public testing::Test {
 protected:
  ChannelSendBurstTest()
      : first(first_sender, kBufferSize4KB, kBufferSize4KB),
        second(second_sender, kBufferSize4KB, kBufferSize4KB) {}

  void SetUp() override {
    first_id = first.connect();
    second_id = second.connect();
    for (int attempt = 0; attempt < 10; ++attempt) {
      exchange();
      if (first.getLinkStatus(first_id) == LinkStatus::kConnected &&
          second.getLinkStatus(second_id) == LinkStatus::kConnected) {
        return;
      }
    }
    FAIL() << "Handshake did not finish";
  }

  // Drives both channels without worker threads or timing-dependent scheduling.
  void exchange() {
    first.trySend();
    first_sender.deliverTo(second);
    second.trySend();
    second_sender.deliverTo(first);
  }

  QueuedPacketSender first_sender;
  QueuedPacketSender second_sender;
  Channel first;
  Channel second;
  uint32_t first_id = 0;
  uint32_t second_id = 0;
};

// Verifies that a sender amortizes control work across a bounded burst while
// preserving unsent packets for its next pass.
TEST_F(ChannelSendBurstTest, SendsEightDataPacketsPerPass) {
  std::vector<roo::byte> bytes(12 * 248, roo::byte{0x5a});
  roo_io::Status status = roo_io::kOk;
  ASSERT_EQ(first.write(bytes.data(), bytes.size(), first_id, status),
            bytes.size());
  ASSERT_EQ(status, roo_io::kOk);
  first.trySend();
  EXPECT_EQ(first_sender.dataPackets(), 8u);
  first_sender.deliverTo(second);
  first.trySend();
  EXPECT_EQ(first_sender.dataPackets(), 4u);
}

// Verifies that bounded bursts still carry ACKs and flow control in both
// directions, and that the application receives complete, ordered payloads.
TEST_F(ChannelSendBurstTest, BothDirectionsContinueAfterBurst) {
  std::vector<roo::byte> sent(12 * 248);
  for (size_t i = 0; i < sent.size(); ++i) {
    sent[i] = static_cast<roo::byte>(i * 73 + 19);
  }
  roo_io::Status status = roo_io::kOk;
  ASSERT_EQ(first.write(sent.data(), sent.size(), first_id, status),
            sent.size());
  ASSERT_EQ(second.write(sent.data(), sent.size(), second_id, status),
            sent.size());
  for (int i = 0; i < 3; ++i) exchange();
  std::vector<roo::byte> received(sent.size());
  ASSERT_EQ(first.tryRead(received.data(), received.size(), first_id, status),
            sent.size());
  EXPECT_EQ(received, sent);
  ASSERT_EQ(second.tryRead(received.data(), received.size(), second_id, status),
            sent.size());
  EXPECT_EQ(received, sent);
  for (int i = 0; i < 2; ++i) exchange();
  EXPECT_EQ(first.packets_delivered(), 12u);
  EXPECT_EQ(second.packets_delivered(), 12u);
}

}  // namespace
}  // namespace roo_transport
