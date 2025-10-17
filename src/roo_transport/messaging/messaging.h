#pragma once

#include <stdint.h>

#include <functional>

#include "roo_backport.h"
#include "roo_backport/byte.h"
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
  class Receiver {
   public:
    virtual ~Receiver() = default;

    // Called when a new message is received.
    virtual void received(const roo::byte* data, size_t len) = 0;

    // Notifies the recipient that the underlying channel has been reset, and
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

  virtual ~Messaging() { end(); }

  // Can be called only once.
  virtual void begin(Receiver& receiver) = 0;

  // Should be idempotent (OK to call multiple times).
  virtual void end() {}

  // Sends the specified message (unconditionally).
  virtual void send(const roo::byte* data, size_t size) = 0;

  // Sends the specified message, conditioned on successful delivery of the
  // preceding message. That is, if the recipient gets reset in between, the
  // subsequent continuation messages do not get delivered. This functionality
  // allows the sender to send atomic sequences of messages that get delivered
  // wholly or not at all. This can be useful e.g. for breaking up large
  // messages into smaller chunks.
  virtual void sendContinuation(const roo::byte* data, size_t size) = 0;

 protected:
  Messaging() = default;
};

}  // namespace roo_transport
