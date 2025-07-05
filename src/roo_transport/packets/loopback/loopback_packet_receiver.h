#pragma once

#include <memory>

#include "roo_transport/packets/packet_receiver.h"

namespace roo_transport {

class LoopbackPacketReceiver : public PacketReceiver {
 public:
  LoopbackPacketReceiver(ReceiverFn receiver_fn = nullptr);

  bool tryReceive() override;

  void setReceiverFn(ReceiverFn receiver_fn) override;

 private:
  friend class LoopbackPacketSender;

  // Called by the loopback sender.
  void packetReceived(const roo::byte* buf, size_t size);

  ReceiverFn receiver_fn_;

  std::unique_ptr<roo::byte[]> buffer_;
  size_t size_;
};

}  // namespace roo_transport