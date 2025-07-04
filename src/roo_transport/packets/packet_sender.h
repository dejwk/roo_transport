#pragma once

#include <memory>

#include "roo_io/core/output_stream.h"

namespace roo_io {

// Abstraction for sending packets over some medium.
//
// The caller needs to provide data in packets up to kMaxPacketSize (250
// bytes). The transport guarantees that packets are transmitted correctly or
// not at all. That is, if data corruption is detected in a packet, the entire
// packet is dropped.
class PacketSender {
 public:
  // Maximum size of the packet that can be sent.
  constexpr static int kMaxPacketSize = 250;

  virtual ~PacketSender() = default;

  // Sends the specified data packet.
  virtual void send(const roo::byte* buf, size_t len) = 0;

  // Flushes out the output.
  virtual void flush() {}
};

}  // namespace roo_io