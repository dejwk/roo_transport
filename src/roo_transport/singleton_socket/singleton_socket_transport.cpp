#include "roo_transport/singleton_socket/singleton_socket_transport.h"

namespace roo_transport {

SingletonSocketTransport::SingletonSocketTransport(PacketSender& sender,
                                                   PacketReceiver& receiver,
                                                   unsigned int sendbuf_log2,
                                                   unsigned int recvbuf_log2,
                                                   std::string label)
    : sender_(sender),
      receiver_(receiver),
      channel_(sender_, receiver_, sendbuf_log2, recvbuf_log2) {}

void SingletonSocketTransport::readData() { channel_.tryRecv(); }

SingletonSocket SingletonSocketTransport::connectAsync() {
  uint32_t my_stream_id = channel_.connect();
  return SingletonSocket(channel_, my_stream_id);
}

SingletonSocket SingletonSocketTransport::connect() {
  SingletonSocket conn = connectAsync();
  conn.awaitConnected();
  return conn;
}

void SingletonSocketTransport::loop() { channel_.loop(); }

}  // namespace roo_transport
