#include "gtest/gtest.h"
#include "roo_transport/link/internal/protocol.h"
#include "roo_transport/link/internal/thread_safe/channel.h"

namespace roo_transport {
namespace {
class DiscardSender : public PacketSender {
 public:
  void send(const roo::byte*, size_t) override {}
};

class LinkProtocolTest : public testing::Test {
 protected:
  DiscardSender sender;
  Channel channel{sender, kBufferSize4KB, kBufferSize4KB};
  uint32_t id = 0;
  bool peer_control = false;

  void SetUp() override {
    id = channel.connect();
    uint32_t peer_id = id == 1 ? 2 : 1;
    peer_control = peer_id > id;
    roo::byte handshake[11];
    roo_io::StoreBeU16(
        internal::FormatPacketHeader(100, internal::kHandshakePacket, false),
        handshake);
    roo_io::StoreBeU32(peer_id, handshake + 2);
    roo_io::StoreBeU32(id, handshake + 6);
    handshake[10] = roo::byte{4};
    channel.packetReceived(handshake, sizeof(handshake));
    ASSERT_EQ(LinkStatus::kConnected, channel.getLinkStatus(id));
  }

  void header(roo::byte* packet, internal::PacketType type, int seq = 100) {
    roo_io::StoreBeU16(internal::FormatPacketHeader(seq, type, peer_control),
                       packet);
  }
};

TEST_F(LinkProtocolTest, DropsTruncatedHeaders) {
  channel.packetReceived(nullptr, 0);
  for (int type = 0; type < 8; ++type) {
    auto packet = std::unique_ptr<roo::byte[]>(new roo::byte[1]);
    packet[0] = static_cast<roo::byte>((type << 4) | (peer_control ? 128 : 0));
    channel.packetReceived(packet.get(), 1);
  }
  EXPECT_EQ(LinkStatus::kConnected, channel.getLinkStatus(id));
}

TEST_F(LinkProtocolTest, DropsEmptyAndOversizedDataWithoutConsumingSequence) {
  roo::byte packet[251] = {};
  header(packet, internal::kDataPacket);
  channel.packetReceived(packet, 2);
  channel.packetReceived(packet, sizeof(packet));
  packet[2] = roo::byte{42};
  channel.packetReceived(packet, 3);
  roo::byte result[1];
  roo_io::Status status = roo_io::kOk;
  EXPECT_EQ(1u, channel.tryRead(result, 1, id, status));
  EXPECT_EQ(roo::byte{42}, result[0]);
  EXPECT_EQ(roo_io::kOk, status);
}

TEST_F(LinkProtocolTest, EmptyFinalPacketStillClosesStream) {
  roo::byte packet[2];
  header(packet, internal::kFinPacket);
  channel.packetReceived(packet, 2);
  roo::byte result[1];
  roo_io::Status status = roo_io::kOk;
  EXPECT_EQ(0u, channel.tryRead(result, 1, id, status));
  // Stream status is updated on the read following consumption of FIN.
  EXPECT_EQ(0u, channel.tryRead(result, 1, id, status));
  EXPECT_EQ(roo_io::kEndOfStream, status);
}
TEST(LinkBufferSize, EveryAdvertisedSizeConstructsSuccessfully) {
  DiscardSender sender;
  for (auto size :
       {kBufferSize256B, kBufferSize512B, kBufferSize1KB, kBufferSize2KB,
        kBufferSize4KB, kBufferSize8KB, kBufferSize16KB, kBufferSize32KB,
        kBufferSize64KB, kBufferSize128KB, kBufferSize256KB}) {
    Channel channel(sender, size, size);
  }
}

TEST(LinkBufferSize, MaximumWindowRestoresSequenceNumbersAcrossWrap) {
  for (uint16_t start : {0u, 3500u, 65000u}) {
    internal::RingBuffer ring(kMaxLinkBufferSizeLog2, start);
    EXPECT_EQ(1024, ring.capacity());
    for (int i = 0; i < 1024; ++i) ring.push();
    for (int i = -1024; i < 2048; ++i) {
      internal::SeqNum expected = internal::SeqNum(start) + i;
      EXPECT_EQ(expected, ring.restorePosHighBits(expected.raw() & 0xFFF, 12));
    }
    for (int i = 0; i < 1024; ++i) ring.pop();
    EXPECT_TRUE(ring.empty());
  }
}

TEST(LinkBufferSize, RejectsUnsupportedPeerWindowsBeforeConnecting) {
  DiscardSender sender;
  Channel channel(sender, kBufferSize4KB, kBufferSize4KB);
  uint32_t id = channel.connect();
  roo::byte handshake[11] = {};
  roo_io::StoreBeU16(
      internal::FormatPacketHeader(100, internal::kHandshakePacket, false),
      handshake);
  roo_io::StoreBeU32(id == 1 ? 2 : 1, handshake + 2);
  roo_io::StoreBeU32(id, handshake + 6);
  for (int size = 11; size < 16; ++size) {
    handshake[10] = static_cast<roo::byte>(size);
    channel.packetReceived(handshake, sizeof(handshake));
    EXPECT_EQ(LinkStatus::kConnecting, channel.getLinkStatus(id));
  }
  handshake[10] = static_cast<roo::byte>(kMaxLinkBufferSizeLog2);
  channel.packetReceived(handshake, sizeof(handshake));
  EXPECT_EQ(LinkStatus::kConnected, channel.getLinkStatus(id));
}
}  // namespace
}  // namespace roo_transport
