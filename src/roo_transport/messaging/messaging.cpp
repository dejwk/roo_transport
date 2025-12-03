#include "roo_transport/messaging/messaging.h"

#include "roo_logging.h"

namespace roo_transport {

void Messaging::received(ConnectionId connection_id, ChannelId channel_id,
                         const roo::byte* data, size_t len) {
  auto it = receivers_.find(channel_id);
  if (it == receivers_.end()) {
    LOG(WARNING) << "Messaging: received message for unknown channel "
                 << (int)channel_id;
  } else {
    // Dispatch the message to the appropriate channel receiver.
    it->second->received(connection_id, data, len);
  }
}

void Messaging::reset(ConnectionId connection_id) {
  for (auto& entry : receivers_) {
    entry.second->reset(connection_id);
  }
}

void Messaging::registerChannel(Channel& channel) {
  CHECK(receivers_.insert({channel.id_, &channel}).second)
      << "Channel ID " << (int)channel.id_ << " is already registered.";
}

void Messaging::unregisterChannel(Channel& channel) {
  CHECK(receivers_.erase(channel.id_))
      << "Channel ID " << (int)channel.id_ << " is not registered.";
}

}  // namespace roo_transport