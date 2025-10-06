#pragma once

#include <stdint.h>

#include <functional>

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
  enum SendMode {
    // A regular, standalone message.
    kRegular,

    // A message that is part of a larger sequence of messages. The framework
    // will deliver a continuation message only if the preceding message has
    // been successfully delivered as well (i.e., if the underlying channel has
    // not been reset in the meantime).
    kContinuation,
  };

  class Receiver {
   public:
    virtual ~Receiver() = default;

    // Called when a new message is received.
    virtual void received(const void* data, size_t len) = 0;

    // Notifies the recipient that the underlying channel has been reset, and
    // that any state associated with previously received messages should be
    // cleared.
    virtual void reset() {}
  };

  class SimpleReceiver : public Receiver {
   public:
    using Fn = std::function<void(const void* data, size_t len)>;
    explicit SimpleReceiver(Fn fn) : fn_(std::move(fn)) {}
    void received(const void* data, size_t len) override { fn_(data, len); }

   private:
    Fn fn_;
  };

  Messaging(Receiver& receiver) : receiver_(receiver) {}

  virtual ~Messaging() = default;
  virtual void send(const void* data, size_t size, SendMode send_mode) = 0;

 protected:
  void reset() { receiver_.reset(); }
  void receive(const void* data, size_t len) { receiver_.received(data, len); }

 private:
  Receiver& receiver_;
};

}  // namespace roo_transport
