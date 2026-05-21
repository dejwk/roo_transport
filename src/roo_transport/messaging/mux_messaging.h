#pragma once

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
#include "roo_transport/messaging/messaging.h"

namespace roo_transport {

/// Multiplexes up to 256 logical messaging channels over one `Messaging` link.
class MuxMessaging {
 public:
  using ChannelId = uint8_t;

  class Channel;

  /// Creates a multiplexer over `messaging`.
  MuxMessaging(Messaging& messaging);
  /// Destroys the multiplexer and unregisters its channels.
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

  /// Called by `Channel` constructor.
  void registerChannel(Channel& channel);

  /// Called by `Channel` destructor.
  void unregisterChannel(Channel& channel);

  void received(Messaging::ConnectionId connection_id, ChannelId channel_id,
                const roo::byte* data, size_t len);

  void reset(Messaging::ConnectionId connection_id);

  Messaging& messaging_;
  Dispatcher dispatcher_;
  roo_collections::FlatSmallHashMap<ChannelId, Channel*> receivers_;
};

/// Logical messaging channel hosted by `MuxMessaging`.
class MuxMessaging::Channel : public Messaging {
 public:
  using Messaging::send;
  using Messaging::sendContinuation;

  /// Creates a logical channel with id `id`.
  Channel(MuxMessaging& messaging, ChannelId id)
      : messaging_(messaging), id_(id) {
    messaging_.registerChannel(*this);
  }

  /// Destroys the channel and unregisters it from the multiplexer.
  ~Channel() { messaging_.unregisterChannel(*this); }

  /// Sends one multiplexed message on this channel.
  bool send(const roo::byte* header, size_t header_size,
            const roo::byte* payload, size_t payload_size,
            ConnectionId* connection_id) override;

  /// Sends continuation data on an existing multiplexed connection.
  bool sendContinuation(ConnectionId connection_id, const roo::byte* header,
                        size_t header_size, const roo::byte* payload,
                        size_t payload_size) override;

 private:
  friend class MuxMessaging;

  MuxMessaging& messaging_;
  ChannelId id_;
};

}  // namespace roo_transport
