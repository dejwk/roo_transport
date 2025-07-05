#include "roo_transport/singleton_socket/singleton_socket.h"

namespace roo_io {

SingletonSocket::SingletonSocket() : channel_(nullptr), my_stream_id_(0) {}

SingletonSocket::SingletonSocket(Channel& channel, uint32_t my_stream_id)
    : channel_(&channel),
      my_stream_id_(my_stream_id),
      in_(*channel_, my_stream_id),
      out_(*channel_, my_stream_id) {}

bool SingletonSocket::isConnecting() {
  return channel_ == nullptr ? false : channel_->isConnecting(my_stream_id_);
}

void SingletonSocket::awaitConnected() {
  if (channel_ == nullptr) return;
  channel_->awaitConnected(my_stream_id_);
}

bool SingletonSocket::awaitConnected(roo_time::Interval timeout) {
  if (channel_ == nullptr) return true;
  return channel_->awaitConnected(my_stream_id_, timeout);
}

}  // namespace roo_io
