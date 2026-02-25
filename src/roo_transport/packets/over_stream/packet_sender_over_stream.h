#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io/core/output_stream.h"
#include "roo_transport/packets/packet_sender.h"

namespace roo_transport {

/// Sends packets via a potentially unreliable stream (for example UART/Serial)
/// while adding transport framing/integrity metadata.
///
/// Uses 32-bit hashes for integrity and COBS framing so the receiver can
/// recover packet boundaries under loss/corruption.
class PacketSenderOverStream : public PacketSender {
 public:
  /// Maximum payload size of one packet.
  constexpr static int kMaxPacketSize = 250;

  /// Creates sender writing framed transport packets to `out`.
  ///
  /// Stream may be unreliable (drop/corrupt/reorder bytes).
  PacketSenderOverStream(roo_io::OutputStream& out);

  /// Sends one packet payload.
  void send(const roo::byte* buf, size_t len) override;

  void flush() override { out_.flush(); }

 private:
  roo_io::OutputStream& out_;
  /// Work buffer allocated in constructor.
  std::unique_ptr<roo::byte[]> buf_;
};

}  // namespace roo_transport