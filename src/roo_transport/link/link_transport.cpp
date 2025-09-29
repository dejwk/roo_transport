#include "roo_transport/link/link_transport.h"

namespace roo_transport {

LinkTransport::LinkTransport(PacketSender& sender, PacketReceiver& receiver,
                             unsigned int sendbuf_log2,
                             unsigned int recvbuf_log2)
    : sender_(sender),
      receiver_(receiver),
      channel_(sender_, receiver_, sendbuf_log2, recvbuf_log2) {}

void LinkTransport::readData() { channel_.tryRecv(); }

Link LinkTransport::connectAsync() {
  uint32_t my_stream_id = channel_.connect();
  return Link(channel_, my_stream_id);
}

Link LinkTransport::connect() {
  Link conn = connectAsync();
  conn.awaitConnected();
  return conn;
}

void LinkTransport::loop() { channel_.loop(); }

}  // namespace roo_transport
