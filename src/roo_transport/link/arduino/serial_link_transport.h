#pragma once

#if (defined ARDUINO)

#include "roo_io/uart/arduino/serial_input_stream.h"
#include "roo_io/uart/arduino/serial_output_stream.h"
#include "roo_transport/link/arduino/link_stream.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

namespace roo_transport {

#if (defined ESP32 || defined ROO_TESTING)

template <typename SerialType>
class SerialLinkTransport {
 public:
  SerialLinkTransport(SerialType& serial, roo::string_view name,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial),
        output_(serial_),
        input_(serial_),
        sender_(output_),
        receiver_(input_),
        transport_(sender_, name, sendbuf, recvbuf) {}

  SerialLinkTransport(SerialType& serial,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : SerialLinkTransport(serial, "", sendbuf, recvbuf) {}

  void begin() {
    transport_.begin();
    serial_.onReceive([this]() {
      receiver_.tryReceive([this](const roo::byte* buf, size_t len) {
        transport_.processIncomingPacket(buf, len);
      });
    });
    serial_.onReceiveError([this](hardwareSerial_error_t) {
      receiver_.tryReceive([this](const roo::byte* buf, size_t len) {
        transport_.processIncomingPacket(buf, len);
      });
    });
  }

  void end() {
    serial_.onReceive(nullptr);
    serial_.end();
  }

  LinkStream connect(std::function<void()> disconnect_fn = nullptr) {
    LinkStream link = connectAsync(std::move(disconnect_fn));
    link.awaitConnected();
    return LinkStream(std::move(link));
  }

  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr) {
    return LinkStream(transport_.connect(std::move(disconnect_fn)));
  }

  uint32_t packets_sent() const { return transport_.packets_sent(); }
  uint32_t packets_delivered() const { return transport_.packets_delivered(); }
  uint32_t packets_received() const { return transport_.packets_received(); }

  size_t receiver_bytes_received() const { return receiver_.bytes_received(); }
  size_t receiver_bytes_accepted() const { return receiver_.bytes_accepted(); }

  LinkTransport& transport() { return transport_; }

 private:
  SerialType& serial_;
  roo_io::ArduinoSerialOutputStream output_;
  roo_io::ArduinoSerialInputStream input_;
  PacketSenderOverStream sender_;
  PacketReceiverOverStream receiver_;

  LinkTransport transport_;
};

class ReliableSerialTransport : public SerialLinkTransport<decltype(Serial)> {
 public:
  ReliableSerialTransport(LinkBufferSize sendbuf = kBufferSize4KB,
                          LinkBufferSize recvbuf = kBufferSize4KB)
      : SerialLinkTransport<decltype(Serial)>(Serial, sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
class ReliableSerial1Transport : public SerialLinkTransport<decltype(Serial1)> {
 public:
  ReliableSerial1Transport(LinkBufferSize sendbuf = kBufferSize4KB,
                           LinkBufferSize recvbuf = kBufferSize4KB)
      : SerialLinkTransport<decltype(Serial1)>(Serial1, sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
class ReliableSerial2Transport : public SerialLinkTransport<decltype(Serial2)> {
 public:
  ReliableSerial2Transport(LinkBufferSize sendbuf = kBufferSize4KB,
                           LinkBufferSize recvbuf = kBufferSize4KB)
      : SerialLinkTransport<decltype(Serial2)>(Serial2, sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 2

#endif  // ESP32

}  // namespace roo_transport

#endif  // defined(ARDUINO)