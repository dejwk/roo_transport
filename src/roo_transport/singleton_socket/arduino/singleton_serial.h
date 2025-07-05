#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_io_arduino/stream/arduino_stream_input_stream.h"
#include "roo_io_arduino/stream/arduino_stream_output_stream.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"
#include "roo_transport/singleton_socket/arduino/singleton_serial_socket.h"
#include "roo_transport/singleton_socket/singleton_socket_transport.h"

namespace roo_io {

class SingletonSerial {
 public:
  SingletonSerial(decltype(Serial1)& serial, unsigned int sendbuf_log2,
                  unsigned int recvbuf_log2, std::string label = "");

  void begin() { transport_.begin(); }

  // void loop();

  SingletonSerialSocket connect();
  SingletonSerialSocket connectAsync();

  uint32_t packets_sent() const { return transport_.packets_sent(); }
  uint32_t packets_delivered() const { return transport_.packets_delivered(); }
  uint32_t packets_received() const { return transport_.packets_received(); }

  size_t receiver_bytes_received() const { return receiver_.bytes_received(); }
  size_t receiver_bytes_accepted() const { return receiver_.bytes_accepted(); }

 private:
  roo_io::ArduinoStreamOutputStream output_;
  roo_io::ArduinoStreamInputStream input_;
  roo_io::PacketSenderOverStream sender_;
  roo_io::PacketReceiverOverStream receiver_;

  SingletonSocketTransport transport_;
};

}  // namespace roo_io

#endif  // defined(ARDUINO)