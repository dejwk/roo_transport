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

// Generic implementation of the link stream transport, over an arbitrary
// Arduino Stream. It uses default Arduino APIs to read from and to write to the
// stream. The user needs to keep calling tryReceive() or receive() to process
// incoming packets.
//
// The class does not provide a stream interface itself. Instead, use LinkStream
// returned by connect() or connectAsync() methods to read and write data over
// the reliable link.
class LinkStreamTransport {
 public:
  LinkStreamTransport(Stream& stream, LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB);

  void begin();

  // Establishes a new connection and returns the LinkStream object representing
  // it. The optional function parameter will be called when the link gets
  // disconnected.
  LinkStream connect(std::function<void()> disconnect_fn = nullptr);

  // Establishes a new connection asynchronously and returns the LinkStream
  // object representing it. Until the connection is established, the link will
  // be in the "connecting" state. The optional function parameter will be
  // called when the link gets disconnected.
  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr);

  // Establishes a new connection and returns the LinkStream object representing
  // it. If the peer attempts reconnection (e.g. after a reset), the program
  // will terminate (usually to reconnect after reboot).
  LinkStream connectOrDie();

  LinkTransport& transport() { return transport_; }

  // Allow implicit conversion to LinkTransport&, so that this Arduino wrapper
  // can be used seamlessly in place of LinkTransport when a reference to the
  // latter is needed (e.g., when constructing LinkMessaging).
  operator LinkTransport&() { return transport_; }

  // Attempts to receive one or more packets without blocking. Returns the
  // number of packets received.
  size_t tryReceive();

  // Attempts to receive one or more packets, blocking if necessary until at
  // least one packet is received. Returns the number of packets received.
  size_t receive();

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