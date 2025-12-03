#pragma once

#include <stdint.h>

#include <functional>
#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_collections.h"
#include "roo_collections/flat_small_hash_map.h"
namespace roo_transport {

// Abstract interface for messaging over a reliable channel. The messages are
// byte arrays of arbitrary length. The implementation must guarantee
// in-order delivery and message integrity.
//
// Messages may get lost when the underlying channel gets reset (e.g. if the
// peer reconnects after a restart). The interface provides both the sender and
// the receiver with means to handle message loss.
class Messaging {
 public:
  using ChannelId = uint8_t;
  using ConnectionId = uint32_t;

  class Receiver {
   public:
    virtual ~Receiver() = default;

    // Called when a new message is received on the channel. The (receiver-side)
    // connection ID can be used to ensure that response messages (e.g. RPC
    // responses) are sent on the same connection, using
    // channel.sendContinuation().
    virtual void received(ConnectionId connection_id, const roo::byte* data,
                          size_t len) = 0;

    // Notifies the recipient that the underlying connection has been closed,
    // and that any state associated with previously received messages using
    // that connection ID should be cleared.
    virtual void reset(ConnectionId connection_id) {}
  };

  class SimpleReceiver : public Receiver {
   public:
    using Fn = std::function<void(ConnectionId connection_id,
                                  const roo::byte* data, size_t len)>;
    explicit SimpleReceiver(Fn fn) : fn_(std::move(fn)) {}
    void received(ConnectionId connection_id, const roo::byte* data,
                  size_t len) override {
      fn_(connection_id, data, len);
    }

   private:
    Fn fn_;
  };

  class Channel;

  virtual ~Messaging() { end(); }

  // Can be called only once.
  virtual void begin() = 0;

  // Should be idempotent (OK to call multiple times).
  virtual void end() {}

 protected:
  Messaging() = default;

  void received(ConnectionId connection_id, ChannelId channel_id,
                const roo::byte* data, size_t len);

  void reset(ConnectionId connection_id);

 private:
  friend class Channel;

  // Sends the specified message (unconditionally). See Channel::send().
  virtual ConnectionId send(ChannelId channel_id, const roo::byte* data,
                            size_t size) = 0;

  // Sends the specified message, using the specified sender-side connection ID.
  // See See Channel::sendContinuation().
  virtual bool sendContinuation(ConnectionId connection_id,
                                ChannelId channel_id, const roo::byte* data,
                                size_t size) = 0;

  // Called by Messaging::Channel constructor.
  void registerChannel(Channel& channel);

  // Called by Messaging::Channel destructor.
  void unregisterChannel(Channel& channel);

  roo_collections::FlatSmallHashMap<ChannelId, Channel*> receivers_;
};

class Messaging::Channel {
 public:
  Channel(Messaging& messaging, ChannelId id)
      : messaging_(messaging), id_(id), receiver_(nullptr) {
    messaging_.registerChannel(*this);
  }
  ~Channel() { messaging_.unregisterChannel(*this); }

  void setReceiver(Receiver& receiver) { receiver_ = &receiver; }
  void unsetReceiver() { receiver_ = nullptr; }

  // Sends the specified message (unconditionally). Returns the (sender-side)
  // connection ID that was used to send the message. (That connection ID may be
  // later used for sendContinuation; see below).
  ConnectionId send(const roo::byte* data, size_t size) {
    return messaging_.send(id_, data, size);
  }

  // Sends the specified message, using the specified sender-side connection ID.
  // Fails if that connection has been closed. Returns false if send cannot be
  // completed because the connection ID has already been closed; true
  // otherwise. (Note: true response does not guarantee delivery).
  //
  // This method is intended for use:
  // (1) for RPC responses, which should be sent on the same connection as the
  //     request (or not at all),
  // (2) for message strams that should be 'atomic' (sent entirely on the same
  //     connection).
  void sendContinuation(ConnectionId connection_id, const roo::byte* data,
                        size_t size) {
    messaging_.sendContinuation(connection_id, id_, data, size);
  }

 private:
  friend class Messaging;

  void received(ConnectionId connection_id, const roo::byte* data, size_t len) {
    if (receiver_ != nullptr) {
      receiver_->received(connection_id, data, len);
    }
  }

  void reset(ConnectionId connection_id) {
    if (receiver_ != nullptr) {
      receiver_->reset(connection_id);
    }
  }

  Messaging& messaging_;
  ChannelId id_;
  Receiver* receiver_;
};

}  // namespace roo_transport
