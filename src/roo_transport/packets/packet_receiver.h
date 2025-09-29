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

  // Must be called when there might be new data to read from the input
  // stream. If there is no data on the stream, returns immediately. Otherwise,
  // processes up to 250 bytes of data, and calls the specified callback for
  // each packet received. Returns true if at least one packet was received;
  // false otherwise.
  virtual bool tryReceive(const ReceiverFn& receiver_fn) = 0;
};

}  // namespace roo_transport
