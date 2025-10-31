#include "roo_transport/messaging/messaging.h"

#include "roo_logging.h"

namespace roo_transport {

void Messaging::received(ChannelId channel_id, const roo::byte* data,
                         size_t len) {
  auto it = receivers_.find(channel_id);
  if (it == receivers_.end()) {
    LOG(WARNING) << "Messaging: received message for unknown channel "
                 << (int)channel_id;
  } else {
    // Dispatch the message to the appropriate channel receiver.
    it->second->received(data, len);
  }
}

void Messaging::reset() {
  for (auto& entry : receivers_) {
    entry.second->reset();
  }
}

std::unique_ptr<Messaging::Channel> Messaging::newChannel(
    Messaging::ChannelId channel_id) {
  auto channel = std::make_unique<Messaging::Channel>(*this, channel_id);
  receivers_.insert({channel_id, channel.get()});
  return channel;
}

}  // namespace roo_transport