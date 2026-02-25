#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io.h"
#include "roo_io/core/input_stream.h"
#include "roo_transport/packets/packet_receiver.h"

namespace roo_transport {

/// Receives packets sent by `PacketSenderOverStream` via a potentially
/// unreliable stream (for example UART/Serial).
///
/// Uses 32-bit hashes to validate packet integrity and COBS framing to recover
/// packet boundaries even under byte loss/corruption.
///
/// Delivers only packets that pass integrity checks; corrupted packets are
/// dropped, and packet loss is possible.
class PacketReceiverOverStream : public PacketReceiver {
 public:
  /// Creates a receiver reading framed bytes from `in`.
  PacketReceiverOverStream(roo_io::InputStream& in);

  size_t tryReceive(const ReceiverFn& receiver_fn) override;

  size_t receive(const ReceiverFn& receiver_fn) override;

  /// Returns total raw bytes read from the underlying stream.
  ///
  /// Includes bytes that were part of malformed/corrupted packets.
  size_t bytes_received() const { return bytes_received_; }

  /// Returns total bytes accepted as valid framed packets.
  ///
  /// This is transport-level byte count (framing/hash included), not just
  /// payload length.
  size_t bytes_accepted() const { return bytes_accepted_; }

 private:
  // Processes up to `len` bytes of incoming data stored in `tmp_`, calling
  // `receiver_fn` for each valid packet received. Returns the number of packets
  // delivered.
  size_t processIncoming(size_t len, const ReceiverFn& receiver_fn);

  // Processes a complete packet stored in `buf` of size `size`, calling
  // `receiver_fn` if the packet is valid. Returns true if the packet was
  // accepted; false if it was rejected due to data corruption or other errors.
  bool processPacket(roo::byte* buf, size_t size,
                     const ReceiverFn& receiver_fn);

  roo_io::InputStream& in_;
  std::unique_ptr<roo::byte[]> buf_;
  std::unique_ptr<roo::byte[]> tmp_;
  size_t pos_;

  size_t bytes_received_;
  size_t bytes_accepted_;
};

}  // namespace roo_transport