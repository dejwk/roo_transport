#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_io.h"
#include "roo_io/stream/arduino/stream_input_stream.h"
#include "roo_io/stream/arduino/stream_output_stream.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/link_stream.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

namespace roo_transport {

/// Reliable link transport over an arbitrary Arduino `Stream`.
///
/// Uses the default Arduino stream APIs to read and write framed packets. The
/// caller must keep invoking `tryReceive()` or `receive()` so incoming packets
/// are processed.
class LinkStreamTransport {
 public:
  /// Creates a transport that reads and writes framed packets on `stream`.
  LinkStreamTransport(Stream& stream, LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB);

  /// Starts the underlying `LinkTransport`.
  void begin();

  /// Establishes a new connection and returns the resulting `LinkStream`.
  ///
  /// Blocks until the handshake finishes. If supplied, `disconnect_fn` is
  /// called when that link later disconnects.
  LinkStream connect(std::function<void()> disconnect_fn = nullptr);

  /// Establishes a new connection without waiting for completion.
  ///
  /// The returned stream remains in `kConnecting` until the handshake
  /// completes. If supplied, `disconnect_fn` is called when that link later
  /// disconnects.
  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr);

  /// Establishes a new connection and terminates if the peer later resets.
  LinkStream connectOrDie();

  /// Returns the underlying transport.
  LinkTransport& transport() { return transport_; }

  /// Returns the underlying transport by implicit conversion.
  operator LinkTransport&() { return transport_; }

  /// Processes available packets without blocking.
  ///
  /// @return Number of packets received.
  size_t tryReceive();

  /// Processes packets until at least one packet is received.
  ///
  /// @return Number of packets received.
  size_t receive();

  /// Returns a stats view for the underlying transport.
  LinkTransport::StatsMonitor statsMonitor() {
    return LinkTransport::StatsMonitor(transport_);
  }

 private:
  Stream& stream_;
  roo_io::ArduinoStreamOutputStream output_;
  roo_io::ArduinoStreamInputStream input_;
  PacketSenderOverStream sender_;
  PacketReceiverOverStream receiver_;

  LinkTransport transport_;
};

}  // namespace roo_transport

#endif  // defined(ARDUINO)