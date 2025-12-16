#include "roo_transport/messaging/messaging.h"

namespace roo_transport {

void Messaging::received(ConnectionId connection_id, const roo::byte* data,
                         size_t len) {
  if (receiver_ != nullptr) {
    receiver_->received(connection_id, data, len);
  }
}

void Messaging::reset(ConnectionId connection_id) {
  if (receiver_ != nullptr) {
    receiver_->reset(connection_id);
  }
}

}  // namespace roo_transport