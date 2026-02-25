#pragma once

#include <stdint.h>

#include <functional>
#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"

namespace roo_transport {

/// Abstract interface for message exchange over a reliable channel.
///
/// Messages are arbitrary-length byte arrays with in-order, integrity-checked
/// delivery. Messages may be lost across channel reset/reconnect boundaries.
class Messaging {
 public:
  using ConnectionId = uint32_t;

  class Receiver;
  class SimpleReceiver;

  virtual ~Messaging() = default;

  /// Registers message receiver. Call before channel initialization.
  void setReceiver(Receiver& receiver) { receiver_ = &receiver; }

  /// Unregisters receiver.
  void unsetReceiver() { receiver_ = nullptr; }

  /// Sends message with optional header and payload.
  ///
  /// @param connection_id Optional out parameter receiving sender-side
  /// connection id used for this send.
  /// @return true if accepted for send.
  virtual bool send(const roo::byte* header, size_t header_size,
                    const roo::byte* payload, size_t payload_size,
                    ConnectionId* connection_id) = 0;

  /// Sends continuation payload on an existing sender-side connection.
  ///
  /// Intended for connection-affine responses (e.g. RPC) and atomic message
  /// streams.
  virtual bool sendContinuation(ConnectionId connection_id,
                                const roo::byte* header, size_t header_size,
                                const roo::byte* payload,
                                size_t payload_size) = 0;

  /// Convenience overload for header-less messages.
  virtual bool send(const roo::byte* payload, size_t payload_size,
                    ConnectionId* connection_id) {
    return send(nullptr, 0, payload, payload_size, connection_id);
  }

  /// Convenience overload for header-less stateless messages.
  bool send(const roo::byte* payload, size_t payload_size) {
    return send(payload, payload_size, nullptr);
  }

  /// Convenience overload for header-less continuation messages.
  virtual bool sendContinuation(ConnectionId connection_id,
                                const roo::byte* payload, size_t payload_size) {
    return sendContinuation(connection_id, nullptr, 0, payload, payload_size);
  }

 protected:
  Messaging() = default;

  /// Dispatches received message to registered receiver.
  void received(ConnectionId connection_id, const roo::byte* data, size_t len);

  /// Dispatches reset notification to registered receiver.
  void reset(ConnectionId connection_id);

 private:
  Receiver* receiver_ = nullptr;
};

class Messaging::Receiver {
 public:
  virtual ~Receiver() = default;

  /// Called when message is received.
  ///
  /// `connection_id` identifies receiver-side channel context and can be used
  /// for connection-affine responses via `sendContinuation()`.
  virtual void received(ConnectionId connection_id, const roo::byte* data,
                        size_t len) = 0;

  /// Notifies that underlying connection was closed/reset.
  ///
  /// Receiver should clear connection-associated state.
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
