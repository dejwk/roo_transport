#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"
#include "roo_transport/packets/loopback/loopback_packet_receiver.h"
#include "roo_transport/packets/packet_sender.h"

namespace roo_io {

class LoopbackPacketSender : public PacketSender {
 public:
  // Maximum size of the packet that can be sent.
  constexpr static int kMaxPacketSize = 250;

  // Creates the sender that will write packets directly to the specified
  // receiver.
  LoopbackPacketSender(LoopbackPacketReceiver& receiver);

  // Sends the specified data packet.
  void send(const roo::byte* buf, size_t len) override;

 private:
  LoopbackPacketReceiver& receiver_;
};

}  // namespace roo_io