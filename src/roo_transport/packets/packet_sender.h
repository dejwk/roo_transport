#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"

namespace roo_transport {

/// Abstraction for sending packets over an underlying medium.
///
/// Packets are up to `kMaxPacketSize` bytes. Implementations may use
/// unreliable media where loss/corruption can occur.
class PacketSender {
 public:
  /// Maximum packet size that can be sent.
  constexpr static int kMaxPacketSize = 250;

  virtual ~PacketSender() = default;

  /// Sends one data packet.
  virtual void send(const roo::byte* buf, size_t len) = 0;

  /// Flushes pending output.
  virtual void flush() {}
};

}  // namespace roo_transport