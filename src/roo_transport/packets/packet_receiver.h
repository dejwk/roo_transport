#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io/core/input_stream.h"

namespace roo_transport {

/// Abstraction for receiving packets produced by `PacketSender`.
///
/// Data arrives in packets up to 250 bytes. Corrupted packets are dropped;
/// packet loss is possible.
class PacketReceiver {
 public:
  /// Callback invoked for each received packet.
  using ReceiverFn = std::function<void(const roo::byte*, size_t)>;

  /// Receives currently available packets without indefinite blocking.
  ///
  /// @return Number of packets delivered.
  virtual size_t tryReceive(const ReceiverFn& receiver_fn) = 0;

  /// Receives packets, blocking as needed until at least one packet is
  /// delivered, or until stream end/error.
  ///
  /// @return Number of delivered packets, or zero on error/end-of-stream.
  virtual size_t receive(const ReceiverFn& receiver_fn) = 0;
};

}  // namespace roo_transport
