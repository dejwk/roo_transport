#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io.h"
#include "roo_io/core/input_stream.h"
#include "roo_transport/packets/packet_receiver.h"

namespace roo_transport {

// Receives packets sent by PacketSender. Implements data integrity (ensures
// data correctness) over a potentially unreliable underlying stream, such as
// UART/Serial.
//
// The data arrives in packets up to 250 bytes in size. The transport guarantees
// that receives packets have been transmitted correctly, although some packets
// may have gotten lost. If data corruption is detected in a packet, the entire
// packet is dropped.
//
// The underlying implementation uses 32-bit hashes to verify integrity, and
// uses COBS encoding to make sure that the receiver can recognize packet
// boundaries even in case of data loss or corruption.
class PacketReceiverOverStream : public PacketReceiver {
 public:
  // Creates a packet receiver that reads data from the underlying input stream
  // (assumed unreliable), and invoking the specified callback `receiver_fn`
  // when a valid packet is received.
  //
  // The receiver_fn can be left unspecified, and supplied later by calling
  // `setReceiverFn`.
  PacketReceiverOverStream(roo_io::InputStream& in,
                           ReceiverFn receiver_fn = nullptr);

  bool tryReceive() override;

  void setReceiverFn(ReceiverFn receiver_fn) override;

  // Returns the total amount of bytes received, including bytes rejected due to
  // communication errors.
  size_t bytes_received() const { return bytes_received_; }

  // Returns the total amount of bytes correctly retrieved.
  size_t bytes_accepted() const { return bytes_accepted_; }

 private:
  void processPacket(roo::byte* buf, size_t size);

  roo_io::InputStream& in_;
  std::unique_ptr<roo::byte[]> buf_;
  std::unique_ptr<roo::byte[]> tmp_;
  size_t pos_;
  ReceiverFn receiver_fn_;

  size_t bytes_received_;
  size_t bytes_accepted_;
};

}  // namespace roo_transport