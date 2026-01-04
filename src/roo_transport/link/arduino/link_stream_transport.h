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

  LinkStream connect(std::function<void()> disconnect_fn = nullptr);
  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr);
  LinkStream connectOrDie();

  uint32_t packets_sent() const { return transport_.packets_sent(); }
  uint32_t packets_delivered() const { return transport_.packets_delivered(); }
  uint32_t packets_received() const { return transport_.packets_received(); }

  size_t receiver_bytes_received() const { return receiver_.bytes_received(); }
  size_t receiver_bytes_accepted() const { return receiver_.bytes_accepted(); }

  LinkTransport& transport() { return transport_; }

  // Attempts to receive one or more packets without blocking. Returns the
  // number of packets received.
  size_t tryReceive();

  // Attempts to receive one or more packets, blocking if necessary until at
  // least one packet is received. Returns the number of packets received.
  size_t receive();

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