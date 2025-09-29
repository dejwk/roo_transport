#include "roo_transport/packets/loopback/loopback_packet_receiver.h"

#include "roo_collections.h"
#include "roo_collections/hash.h"
#include "roo_io/memory/load.h"
#include "roo_io/third_party/nanocobs/cobs.h"
#include "roo_logging.h"

namespace roo_transport {

LoopbackPacketReceiver::LoopbackPacketReceiver(ReceiverFn receiver_fn)
    : receiver_fn_(receiver_fn), buffer_(new roo::byte[256]), size_(0) {}

void LoopbackPacketReceiver::packetReceived(const roo::byte* buf, size_t size) {
  memcpy(buffer_.get(), buf, size);
  size_ = size;
  tryReceive(receiver_fn_);
}

bool LoopbackPacketReceiver::tryReceive(const ReceiverFn& receiver_fn) {
  if (size_ == 0) return false;
  if (receiver_fn != nullptr) {
    receiver_fn(buffer_.get(), size_);
  }
  size_ = 0;
  return true;
}

}  // namespace roo_transport