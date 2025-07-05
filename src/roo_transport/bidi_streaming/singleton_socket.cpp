#include "roo_transport/bidi_streaming/singleton_socket.h"

namespace roo_io {

SingletonSocket::SingletonSocket(Channel& channel, uint32_t my_stream_id)
    : channel_(channel),
      my_stream_id_(my_stream_id),
      in_(channel_, my_stream_id),
      out_(channel_, my_stream_id) {}

bool SingletonSocket::isConnecting() {
  return channel_.isConnecting(my_stream_id_);
}

void SingletonSocket::awaitConnected() {
  channel_.awaitConnected(my_stream_id_);
}

bool SingletonSocket::awaitConnected(roo_time::Interval timeout) {
  return channel_.awaitConnected(my_stream_id_, timeout);
}

}  // namespace roo_io
