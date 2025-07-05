#include "roo_transport/packets/loopback/loopback_packet_sender.h"
#include "roo_logging.h"

namespace roo_transport {

LoopbackPacketSender::LoopbackPacketSender(LoopbackPacketReceiver& receiver)
    : receiver_(receiver) {}

void LoopbackPacketSender::send(const roo::byte* buf, size_t len) {
  while (len > 250) {
    receiver_.packetReceived(buf, 250);
    len -= 250;
    buf += 250;
  }
  receiver_.packetReceived(buf, len);
}

}  // namespace roo_transport