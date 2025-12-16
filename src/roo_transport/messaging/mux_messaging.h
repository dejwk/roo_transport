#pragma once

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_transport/messaging/messaging.h"

namespace roo_transport {

// Supports multiple independent messaging channels (up to 256) multiplexed over
// a single messaging interface.
class MuxMessaging {
 public:
  using ChannelId = uint8_t;

  class Channel;

  MuxMessaging(Messaging& messaging);
  ~MuxMessaging();

 private:
  friend class Channel;

  class Dispatcher : public Messaging::Receiver {
   public:
    explicit Dispatcher(MuxMessaging& mux) : mux_(mux) {}

    void received(Messaging::ConnectionId connection_id, const roo::byte* data,
                  size_t len) override;

    void reset(Messaging::ConnectionId connection_id) override {
      mux_.reset(connection_id);
    }

   private:
    MuxMessaging& mux_;
  };

  // Called by Messaging::Channel constructor.
  void registerChannel(Channel& channel);

  // Called by Messaging::Channel destructor.
  void unregisterChannel(Channel& channel);

  void received(Messaging::ConnectionId connection_id, ChannelId channel_id,
                const roo::byte* data, size_t len);

  void reset(Messaging::ConnectionId connection_id);

  Messaging& messaging_;
  Dispatcher dispatcher_;
  roo_collections::FlatSmallHashMap<ChannelId, Channel*> receivers_;
};

class MuxMessaging::Channel : public Messaging {
 public:
  Channel(MuxMessaging& messaging, ChannelId id)
      : messaging_(messaging), id_(id) {
    messaging_.registerChannel(*this);
  }

  ~Channel() { messaging_.unregisterChannel(*this); }

  ConnectionId send(const roo::byte* header, size_t header_size,
                    const roo::byte* payload, size_t payload_size) override;

  bool sendContinuation(ConnectionId connection_id, const roo::byte* header,
                        size_t header_size, const roo::byte* payload,
                        size_t payload_size) override;

 private:
  friend class MuxMessaging;

  MuxMessaging& messaging_;
  ChannelId id_;
};

}  // namespace roo_transport
