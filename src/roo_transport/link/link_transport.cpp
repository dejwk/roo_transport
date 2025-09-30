#include "roo_transport/link/link_transport.h"

namespace roo_transport {

LinkTransport::LinkTransport(PacketSender& sender, unsigned int sendbuf_log2,
                             unsigned int recvbuf_log2)
    : sender_(sender), channel_(sender_, sendbuf_log2, recvbuf_log2) {}

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
