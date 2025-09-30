#pragma once

#include <memory>

#include "roo_backport.h"
#include "roo_backport/byte.h"
#include "roo_io/core/input_stream.h"

namespace roo_transport {

// Abstraction to receive packets sent by PacketSenter.
//
// The data arrives in packets up to 250 bytes in size. The transport guarantees
// that receives packets have been transmitted correctly, although some packets
// may have gotten lost. If data corruption is detected in a packet, the entire
// packet is dropped.
class PacketReceiver {
 public:
  // Callback type to be called when a packet arrives.
  using ReceiverFn = std::function<void(const roo::byte*, size_t)>;

  // Attempts to receive as many packets as possible without blocking. Invokes
  // receiver_fn callback on every packet received. Returns the number of
  // packets delivered.
  virtual size_t tryReceive(const ReceiverFn& receiver_fn) = 0;

  // Attempts to receive at least one packet, blocking if necessary. Invokes
  // receiver_fn callback on every packet received. Returns the number of
  // packets delivered (at least one), or zero if no packets were received due
  // to an error (including end-of-stream).
  virtual size_t receive(const ReceiverFn& receiver_fn) = 0;
};

}  // namespace roo_transport
