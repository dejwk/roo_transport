#include "roo_transport/link/link_transport.h"

namespace roo_transport {

LinkTransport::LinkTransport(PacketSender& sender, LinkBufferSize sendbuf,
                             LinkBufferSize recvbuf)
    : sender_(sender), channel_(sender_, sendbuf, recvbuf) {}

void LinkTransport::processIncomingPacket(const roo::byte* buf, size_t len) {
  channel_.packetReceived(buf, len);
}

Link LinkTransport::connectAsync(std::function<void()> disconnect_fn) {
  uint32_t my_stream_id = channel_.connect(std::move(disconnect_fn));
  return Link(channel_, my_stream_id);
}

Link LinkTransport::connect(std::function<void()> disconnect_fn) {
  Link conn = connectAsync(std::move(disconnect_fn));
  conn.awaitConnected();
  return conn;
}

}  // namespace roo_transport
