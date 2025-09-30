#include "roo_transport/link/link_transport.h"

namespace roo_transport {

LinkTransport::LinkTransport(PacketSender& sender, LinkBufferSize sendbuf,
                             LinkBufferSize recvbuf)
    : sender_(sender), channel_(sender_, sendbuf, recvbuf) {}

void LinkTransport::processIncomingPacket(const roo::byte* buf, size_t len) {
  channel_.packetReceived(buf, len);
}

Link LinkTransport::connectAsync() {
  uint32_t my_stream_id = channel_.connect();
  return Link(channel_, my_stream_id);
}

Link LinkTransport::connect() {
  Link conn = connectAsync();
  conn.awaitConnected();
  return conn;
}

}  // namespace roo_transport
