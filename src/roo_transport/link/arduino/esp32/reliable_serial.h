#pragma once

#if (defined ARDUINO)
#if (defined ESP32 || defined ROO_TESTING)

#include "Arduino.h"
#include "hal/uart_types.h"
#include "roo_io/uart/arduino/serial_input_stream.h"
#include "roo_io/uart/arduino/serial_output_stream.h"
#include "roo_io/uart/esp32/uart_input_stream.h"
#include "roo_io/uart/esp32/uart_output_stream.h"
#include "roo_threads.h"
#include "roo_threads/thread.h"
#include "roo_transport/link/arduino/link_stream.h"
#include "roo_transport/link/arduino/link_stream_transport.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

namespace roo_transport {
namespace esp32 {

template <typename SerialType>
/// Common ESP32 serial transport scaffolding for a serial type.
class Esp32SerialLinkTransportBase {
 public:
  /// Creates stream adapters for `serial`.
  Esp32SerialLinkTransportBase(SerialType& serial, uart_port_t ignored)
      : serial_(serial), output_(serial_), input_(serial_) {}

 protected:
  SerialType& serial_;
  roo_io::ArduinoSerialOutputStream output_;
  roo_io::ArduinoSerialInputStream input_;
};

// Specialization for HardwareSerial that uses more efficient UART streams
// (directly using esp-idf UART driver).
template <>
/// ESP32 serial transport scaffolding specialized for `HardwareSerial`.
class Esp32SerialLinkTransportBase<HardwareSerial> {
 public:
  /// Creates stream adapters for `serial` on UART `port`.
  Esp32SerialLinkTransportBase(HardwareSerial& serial, uart_port_t port)
      : serial_(serial), output_(port), input_(port) {}

 protected:
  HardwareSerial& serial_;
  roo_io::Esp32UartOutputStream output_;
  roo_io::Esp32UartInputStream input_;
};

template <typename SerialType>
/// ESP32 serial link transport with receive callbacks wired to the UART.
class Esp32SerialLinkTransport
    : public Esp32SerialLinkTransportBase<SerialType> {
 public:
  /// Creates a transport over `serial` on UART `port`.
  Esp32SerialLinkTransport(SerialType& serial, uart_port_t port,
                           roo::string_view name,
                           LinkBufferSize sendbuf = kBufferSize4KB,
                           LinkBufferSize recvbuf = kBufferSize4KB)
      : Esp32SerialLinkTransportBase<SerialType>(serial, port),
        sender_(this->output_),
        receiver_(this->input_),
        transport_(sender_, name, sendbuf, recvbuf),
        process_fn_([this](const roo::byte* buf, size_t len) {
          transport_.processIncomingPacket(buf, len);
        }) {}

  /// Starts the transport and UART receive callbacks.
  void begin() {
    transport_.begin();
    this->serial_.onReceive([this]() { receiver_.tryReceive(process_fn_); });
    this->serial_.onReceiveError(
        [this](hardwareSerial_error_t) { receiver_.tryReceive(process_fn_); });
  }

  /// Stops the transport and UART receive callbacks.
  void end() {
    this->serial_.onReceive(nullptr);
    this->serial_.onReceiveError(nullptr);
    transport_.end();
  }

  /// Establishes a new connection and waits for it to complete.
  LinkStream connect(std::function<void()> disconnect_fn = nullptr) {
    LinkStream link = connectAsync(std::move(disconnect_fn));
    link.awaitConnected();
    return LinkStream(std::move(link));
  }

  /// Establishes a new connection without waiting for completion.
  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr) {
    return LinkStream(transport_.connect(std::move(disconnect_fn)));
  }

  /// Establishes a new connection and terminates if the peer later resets.
  LinkStream connectOrDie() {
    return connect(
        []() { LOG(FATAL) << "LinkTransport: peer reset; rebooting"; });
  }

  /// Returns the underlying transport.
  LinkTransport& transport() { return transport_; }

  /// Returns the underlying transport by implicit conversion.
  operator LinkTransport&() { return transport_; }

  /// Returns a stats view for the underlying transport.
  LinkTransport::StatsMonitor statsMonitor() {
    return LinkTransport::StatsMonitor(transport_);
  }

 private:
  PacketSenderOverStream sender_;
  PacketReceiverOverStream receiver_;

  LinkTransport transport_;

  std::function<void(const roo::byte* buf, size_t len)> process_fn_;
};

// NOTE: these clases rely on the event task created by the Arduino core. By
// default, that task gets created with a very low stack size of just 2048
// bytes. In practice, it might be insufficient, especially if you enable any
// connection logging. Therefore, it is recommended to increase the stack size
// by adding the following line to your platformio.ini or Arduino build flags:
//
// -D ARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=3072

/// Reliable link transport bound to Arduino `Serial` on ESP32.
class ReliableSerial : public Esp32SerialLinkTransport<decltype(Serial)> {
 public:
  /// Creates a transport named `serial`.
  ReliableSerial(LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial("serial", sendbuf, recvbuf) {}

  /// Creates a transport with a custom diagnostic `name`.
  ReliableSerial(roo::string_view name, LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : Esp32SerialLinkTransport<decltype(Serial)>(Serial, UART_NUM_0, name,
                                                   sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
/// Reliable link transport bound to Arduino `Serial1` on ESP32.
class ReliableSerial1 : public Esp32SerialLinkTransport<decltype(Serial1)> {
 public:
  /// Creates a transport named `serial1`.
  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial1("serial1", sendbuf, recvbuf) {}

  /// Creates a transport with a custom diagnostic `name`.
  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : Esp32SerialLinkTransport<decltype(Serial1)>(Serial1, UART_NUM_1, name,
                                                    sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
/// Reliable link transport bound to Arduino `Serial2` on ESP32.
class ReliableSerial2 : public Esp32SerialLinkTransport<decltype(Serial2)> {
 public:
  /// Creates a transport named `serial2`.
  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial2("serial2", sendbuf, recvbuf) {}

  /// Creates a transport with a custom diagnostic `name`.
  ReliableSerial2(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : Esp32SerialLinkTransport<decltype(Serial2)>(Serial2, UART_NUM_2, name,
                                                    sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 2

}  // namespace esp32
}  // namespace roo_transport

#endif  // defined(ESP32 || defined ROO_TESTING)

#endif  // defined(ARDUINO)
