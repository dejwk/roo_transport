#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io/core/output_stream.h"
#include "roo_transport/packets/packet_sender.h"

namespace roo_transport {

// Implements data integrity (ensures data correctness) over a potentially
// unreliable underlying stream, such as UART/Serial.
//
// The underlying implementation uses 32-bit hashes to verify integrity, and
// uses COBS encoding to make sure that the receiver can recognize packet
// boundaries even in case of data loss or corruption.
class PacketSenderOverStream : public PacketSender {
 public:
  // Maximum size of the packet that can be sent.
  constexpr static int kMaxPacketSize = 250;

  // Creates the sender that will write packets to the underlying output stream
  // (which is assumed to be possibly unreliable, e.g. possibly dropping,
  // confusing, or reordering data.)
  PacketSenderOverStream(roo_io::OutputStream& out);

  // Sends the specified data packet.
  void send(const roo::byte* buf, size_t len) override;

  void flush() override { out_.flush(); }

 private:
  roo_io::OutputStream& out_;
  // Work buffer, allocated in the constructor.
  std::unique_ptr<roo::byte[]> buf_;
};

}  // namespace roo_transport