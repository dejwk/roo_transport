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

  class Receiver {
   public:
    virtual ~Receiver() = default;

    // Called when a new message is received on the channel.
    virtual void received(const roo::byte* data, size_t len) = 0;

    // Notifies the recipient that the underlying transport has been reset, and
    // that any state associated with previously received messages should be
    // cleared.
    virtual void reset() {}
  };

  class SimpleReceiver : public Receiver {
   public:
    using Fn = std::function<void(const roo::byte* data, size_t len)>;
    explicit SimpleReceiver(Fn fn) : fn_(std::move(fn)) {}
    void received(const roo::byte* data, size_t len) override {
      fn_(data, len);
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

  void received(ChannelId channel_id, const roo::byte* data, size_t len);
  void reset();

 private:
  friend class Channel;

  // Sends the specified message (unconditionally).
  virtual void send(ChannelId channel_id, const roo::byte* data,
                    size_t size) = 0;

  // Sends the specified message, conditioned on successful delivery of the
  // preceding message. That is, if the recipient gets reset in between, the
  // subsequent continuation messages do not get delivered. This functionality
  // allows the sender to send atomic sequences of messages that get delivered
  // wholly or not at all. This can be useful e.g. for breaking up large
  // messages into smaller chunks.
  virtual void sendContinuation(ChannelId channel_id, const roo::byte* data,
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
  ~Channel() {
    messaging_.unregisterChannel(*this);
  }

  void setReceiver(Receiver& receiver) { receiver_ = &receiver; }
  void unsetReceiver() { receiver_ = nullptr; }

  // Sends the specified message (unconditionally).
  void send(const roo::byte* data, size_t size) {
    messaging_.send(id_, data, size);
  }

  // Sends the specified message (unconditionally).
  void sendContinuation(const roo::byte* data, size_t size) {
    messaging_.sendContinuation(id_, data, size);
  }

 private:
  friend class Messaging;

  void received(const roo::byte* data, size_t len) {
    if (receiver_ != nullptr) {
      receiver_->received(data, len);
    }
  }

  void reset() {
    if (receiver_ != nullptr) {
      receiver_->reset();
    }
  }

  Messaging& messaging_;
  ChannelId id_;
  Receiver* receiver_;
};

}  // namespace roo_transport
