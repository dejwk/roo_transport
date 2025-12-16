#pragma once

#include <stdint.h>

#include <functional>
#include <memory>

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
  using ConnectionId = uint32_t;

  class Receiver;
  class SimpleReceiver;

  virtual ~Messaging() = default;

  // Should be called before initialization (e.g. begin() etc.)
  void setReceiver(Receiver& receiver) { receiver_ = &receiver; }

  void unsetReceiver() { receiver_ = nullptr; }

  // Sends the specified message (unconditionally). Returns the (sender-side)
  // connection ID that was used to send the message. (That connection ID may be
  // later used for sendContinuation; see below).
  virtual ConnectionId send(const roo::byte* header, size_t header_size,
                            const roo::byte* payload, size_t payload_size) = 0;

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
  virtual bool sendContinuation(ConnectionId connection_id,
                                const roo::byte* header, size_t header_size,
                                const roo::byte* payload,
                                size_t payload_size) = 0;

  // Convenience for header-less messages.
  virtual ConnectionId send(const roo::byte* payload, size_t payload_size) {
    return send(nullptr, 0, payload, payload_size);
  }

  // Convenience for header-less continuation messages.
  virtual bool sendContinuation(ConnectionId connection_id,
                                const roo::byte* payload, size_t payload_size) {
    return sendContinuation(connection_id, nullptr, 0, payload, payload_size);
  }

 protected:
  Messaging() = default;

  // Dispatches a received message to the registered receiver.
  void received(ConnectionId connection_id, const roo::byte* data, size_t len);

  // Dispatches a reset notification to the registered receiver.
  void reset(ConnectionId connection_id);

 private:
  Receiver* receiver_ = nullptr;
};

class Messaging::Receiver {
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

class Messaging::SimpleReceiver : public Messaging::Receiver {
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

}  // namespace roo_transport
