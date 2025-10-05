#include <memory>
#include <unordered_map>

#include "gtest/gtest.h"
#include "roo_io/ringpipe/ringpipe.h"
#include "roo_io/ringpipe/ringpipe_input_stream.h"
#include "roo_io/ringpipe/ringpipe_output_stream.h"
#include "roo_threads/thread.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"
#include "helpers/noisy_output_stream.h"

namespace roo_transport {

class Packet {
 public:
  Packet(size_t size) : size_(size), data_(new roo::byte[size]) {}

  Packet(const roo::byte* data, size_t size)
      : size_(size), data_(new roo::byte[size]) {
    memcpy(data_.get(), data, size);
  }

  Packet(const Packet& other)
      : size_(other.size_), data_(new roo::byte[size_]) {
    memcpy(data_.get(), other.data_.get(), size_);
  }

  Packet& operator=(const Packet& other) {
    if (this != &other) {
      size_ = other.size_;
      data_.reset(new roo::byte[size_]);
      memcpy(data_.get(), other.data_.get(), size_);
    }
    return *this;
  }

  roo::byte* data() { return data_.get(); }
  const roo::byte* data() const { return data_.get(); }
  size_t size() const { return size_; }

 private:
  size_t size_;
  std::unique_ptr<roo::byte[]> data_;
};

inline bool operator==(const Packet& a, const Packet& b) {
  if (a.size() != b.size()) return false;
  return memcmp(a.data(), b.data(), a.size()) == 0;
}

struct PacketHash {
  size_t operator()(const Packet& p) const {
    return std::hash<std::string_view>()(
        std::string_view(reinterpret_cast<const char*>(p.data()), p.size()));
  }
};

inline bool operator!=(const Packet& a, const Packet& b) { return !(a == b); }

Packet RandomPacket() {
  size_t size = 1 + (rand() % PacketSenderOverStream::kMaxPacketSize);
  Packet packet(size);
  for (size_t i = 0; i < size; ++i) {
    packet.data()[i] = static_cast<roo::byte>(rand() & 0xFF);
  }
  return packet;
}

TEST(PacketOverStream, SendReceive) {
  roo_io::RingPipe pipe(128);
  roo_io::RingPipeInputStream input_stream(pipe);

  PacketReceiverOverStream receiver(input_stream);

  const size_t num_packets = 1000;
  std::vector<Packet> packets;
  for (size_t i = 0; i < num_packets; ++i) {
    packets.push_back(RandomPacket());
  }

  roo::thread writer([&pipe, &packets]() {
    roo_io::RingPipeOutputStream output_stream(pipe);
    PacketSenderOverStream sender(output_stream);
    for (const auto& packet : packets) {
      sender.send(packet.data(), packet.size());
      roo::this_thread::yield();
    }
    sender.flush();
    output_stream.close();
  });

  size_t received_count = 0;

  auto receive_fn = [&](const roo::byte* buf, size_t len) {
    EXPECT_EQ(len, packets[received_count].size());
    EXPECT_EQ(memcmp(buf, packets[received_count].data(), len), 0);
    received_count++;
  };

  while (input_stream.status() == roo_io::kOk) {
    receiver.receive(receive_fn);
    roo::this_thread::yield();
  }
  EXPECT_EQ(received_count, num_packets);
  EXPECT_EQ(receiver.bytes_accepted(), receiver.bytes_received());
  EXPECT_EQ(input_stream.status(), roo_io::kEndOfStream);
  input_stream.close();
  writer.join();
}

TEST(PacketOverStream, SendReceiveWithErrors) {
  roo_io::RingPipe pipe(128);
  roo_io::RingPipeInputStream input_stream(pipe);

  PacketReceiverOverStream receiver(input_stream);

  const size_t num_packets = 1000;
  std::vector<Packet> packets;
  for (size_t i = 0; i < num_packets; ++i) {
    packets.push_back(RandomPacket());
  }
  std::unordered_map<Packet, int, PacketHash> packet_indexes;
  for (size_t i = 0; i < num_packets; ++i) {
    packet_indexes[packets[i]] = i;
  }

  roo::thread writer([&pipe, &packets]() {
    roo_io::RingPipeOutputStream pipe_output_stream(pipe);
    NoisyOutputStream output_stream(pipe_output_stream, 10);
    PacketSenderOverStream sender(output_stream);
    for (const auto& packet : packets) {
      sender.send(packet.data(), packet.size());
      roo::this_thread::yield();
    }
    sender.flush();
    output_stream.close();
  });

  int last_received_index = -1;
  size_t received_count = 0;

  auto receive_fn = [&](const roo::byte* buf, size_t len) {
    Packet received(buf, len);
    auto it = packet_indexes.find(received);
    // We expect to receive only valid packets.
    EXPECT_NE(it, packet_indexes.end());
    int received_packet_index = it->second;
    // The packets are expected to be received in order.
    EXPECT_GT(received_packet_index, last_received_index);
    last_received_index = received_packet_index;
    received_count++;
  };

  while (input_stream.status() == roo_io::kOk) {
    receiver.tryReceive(receive_fn);
    roo::this_thread::yield();
  }
  // Some packets may have been lost due to errors.
  EXPECT_LE(received_count, num_packets);
  EXPECT_LE(receiver.bytes_accepted(), receiver.bytes_received());
  EXPECT_EQ(input_stream.status(), roo_io::kEndOfStream);
  input_stream.close();
  writer.join();
}

}  // namespace roo_transport
