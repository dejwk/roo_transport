#include "roo_transport/messaging/mux_messaging.h"

#include "roo_io/memory/load.h"
#include "roo_io/memory/store.h"
#include "roo_logging.h"

namespace roo_transport {

MuxMessaging::MuxMessaging(Messaging& messaging)
    : messaging_(messaging), dispatcher_(*this) {
  messaging_.setReceiver(dispatcher_);
}

MuxMessaging::~MuxMessaging() = default;

void MuxMessaging::Dispatcher::received(Messaging::ConnectionId connection_id,
                                        const roo::byte* data, size_t len) {
  if (len < 1) {
    LOG(WARNING) << "Messaging: received message too short (" << len
                 << " bytes)";
    return;
  }
  ChannelId channel_id = (ChannelId)roo_io::LoadU8(&data[0]);

  mux_.received(connection_id, channel_id, data + 1, len - 1);
}

void MuxMessaging::received(Messaging::ConnectionId connection_id,
                            ChannelId channel_id, const roo::byte* data,
                            size_t len) {
  auto it = receivers_.find(channel_id);
  if (it == receivers_.end()) {
    LOG(WARNING) << "Messaging: received message for unknown channel "
                 << (int)channel_id;
  } else {
    // Dispatch the message to the appropriate channel receiver.
    it->second->received(connection_id, data, len);
  }
}

void MuxMessaging::reset(Messaging::ConnectionId connection_id) {
  for (auto& entry : receivers_) {
    entry.second->reset(connection_id);
  }
}

void MuxMessaging::registerChannel(Channel& channel) {
  CHECK(receivers_.insert({channel.id_, &channel}).second)
      << "Channel ID " << (int)channel.id_ << " is already registered.";
}

void MuxMessaging::unregisterChannel(Channel& channel) {
  CHECK(receivers_.erase(channel.id_))
      << "Channel ID " << (int)channel.id_ << " is not registered.";
}

Messaging::ConnectionId MuxMessaging::Channel::send(const roo::byte* header,
                                                    size_t header_size,
                                                    const roo::byte* payload,
                                                    size_t payload_size) {
  roo::byte new_header[header_size + 1];
  roo_io::StoreU8((uint8_t)id_, &new_header[0]);
  memcpy(&new_header[1], header, header_size);
  return messaging_.messaging_.send(new_header, header_size + 1, payload,
                                    payload_size);
}

bool MuxMessaging::Channel::sendContinuation(
    Messaging::ConnectionId connection_id, const roo::byte* header,
    size_t header_size, const roo::byte* payload, size_t payload_size) {
  roo::byte new_header[header_size + 1];
  roo_io::StoreU8((uint8_t)id_, &new_header[0]);
  memcpy(&new_header[1], header, header_size);
  return messaging_.messaging_.sendContinuation(
      connection_id, new_header, header_size + 1, payload, payload_size);
}

}  // namespace roo_transport