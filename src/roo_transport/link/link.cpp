#include "roo_transport/link/link.h"

namespace roo_transport {

Link::Link() : channel_(nullptr), my_stream_id_(0) {}

Link::Link(Channel& channel, uint32_t my_stream_id)
    : channel_(&channel),
      my_stream_id_(my_stream_id),
      in_(*channel_, my_stream_id),
      out_(*channel_, my_stream_id) {}

Link::Link(Link&& other)
    : channel_(other.channel_),
      in_(std::move(other.in_)),
      out_(std::move(other.out_)) {
  other = Link();
}

Link& Link::operator=(Link&& other) {
  if (&other != this) {
    channel_ = other.channel_;
    in_ = std::move(other.in_);
    out_ = std::move(other.out_);
    other = Link();
  }
  return *this;
}

LinkStatus Link::status() const {
  return channel_ == nullptr ? LinkStatus::kIdle
                             : channel_->getLinkStatus(my_stream_id_);
}

void Link::awaitConnected() {
  if (channel_ == nullptr) return;
  channel_->awaitConnected(my_stream_id_);
}

bool Link::awaitConnected(roo_time::Duration timeout) {
  if (channel_ == nullptr) return true;
  return channel_->awaitConnected(my_stream_id_, timeout);
}

void Link::disconnect() {
  if (channel_ == nullptr) return;
  channel_->disconnect(my_stream_id_);
  channel_ = nullptr;
}

}  // namespace roo_transport
