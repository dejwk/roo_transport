#pragma once

#include <memory>

#include "roo_io/core/input_stream.h"

namespace roo_io {

// Abstraction to receive packets sent by PacketSenter.
//
// The data arrives in packets up to 250 bytes in size. The transport guarantees
// that receives packets have been transmitted correctly, although some packets
// may have gotten lost. If data corruption is detected in a packet, the entire
// packet is dropped.
class PacketReceiver {
 public:
  // Callback type to be called when a packet arrives.
  using ReceiverFn = std::function<void(const byte*, size_t)>;

  // Must be called when there might be new data to read from the input
  // stream. Returns true if a packet was received; false otherwise.
  virtual bool tryReceive() = 0;

  // Sets the new receiver callback, overwriting the previous one (if any).
  virtual void setReceiverFn(ReceiverFn receiver_fn) = 0;
};

}  // namespace roo_io
