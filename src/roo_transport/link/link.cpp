#include "roo_transport/link/link.h"

namespace roo_transport {

Link::Link() : channel_(nullptr), my_stream_id_(0) {}

Link::Link(Channel& channel, uint32_t my_stream_id)
    : channel_(&channel),
      my_stream_id_(my_stream_id),
      in_(*channel_, my_stream_id),
      out_(*channel_, my_stream_id) {}

bool Link::isConnecting() {
  return channel_ == nullptr ? false : channel_->isConnecting(my_stream_id_);
}

void Link::awaitConnected() {
  if (channel_ == nullptr) return;
  channel_->awaitConnected(my_stream_id_);
}

bool Link::awaitConnected(roo_time::Interval timeout) {
  if (channel_ == nullptr) return true;
  return channel_->awaitConnected(my_stream_id_, timeout);
}

}  // namespace roo_transport
