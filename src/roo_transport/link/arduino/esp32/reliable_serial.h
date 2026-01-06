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
class Esp32SerialLinkTransportBase {
 public:
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
class Esp32SerialLinkTransportBase<HardwareSerial> {
 public:
  Esp32SerialLinkTransportBase(HardwareSerial& serial, uart_port_t port)
      : serial_(serial), output_(port), input_(port) {}

 protected:
  HardwareSerial& serial_;
  roo_io::Esp32UartOutputStream output_;
  roo_io::Esp32UartInputStream input_;
};

// Similar to LinkStreamTransport, but specialized for Serial types on ESP32.
// The user still needs to initialize the underlying serial (by calling
// begin()), but there is no need to call receive() or tryReceive() to process
// incoming packets; this class takes care of that by registering the receive
// handlers.
template <typename SerialType>
class Esp32SerialLinkTransport
    : public Esp32SerialLinkTransportBase<SerialType> {
 public:
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

  void begin() {
    transport_.begin();
    this->serial_.onReceive([this]() { receiver_.tryReceive(process_fn_); });
    this->serial_.onReceiveError(
        [this](hardwareSerial_error_t) { receiver_.tryReceive(process_fn_); });
  }

  void end() {
    this->serial_.onReceive(nullptr);
    this->serial_.onReceiveError(nullptr);
    transport_.end();
  }

  // Establishes a new connection and returns the Link object representing it.
  LinkStream connect(std::function<void()> disconnect_fn = nullptr) {
    LinkStream link = connectAsync(std::move(disconnect_fn));
    link.awaitConnected();
    return LinkStream(std::move(link));
  }

  // Establishes a new connection asynchronously and returns the Link object
  // representing it. Until the connection is established, the link will be in
  // the "connecting" state.
  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr) {
    return LinkStream(transport_.connect(std::move(disconnect_fn)));
  }

  // Establishes a new connection and returns the Link object representing it.
  // If the peer attempts reconnection (e.g. after a reset), the program
  // will terminate (usually to reconnect after reboot).
  LinkStream connectOrDie() {
    return connect(
        []() { LOG(FATAL) << "LinkTransport: peer reset; rebooting"; });
  }

  LinkTransport& transport() { return transport_; }

  // Allow implicit conversion to LinkTransport&, so that this Arduino wrapper
  // can be used seamlessly in place of LinkTransport when a reference to the
  // latter is needed (e.g., when constructing LinkMessaging).
  operator LinkTransport&() { return transport_; }

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

#if defined(ROO_TESTING)
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#endif

class ReliableSerial : public Esp32SerialLinkTransport<decltype(Serial)> {
 public:
  ReliableSerial(LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial("serial", sendbuf, recvbuf) {}

  ReliableSerial(roo::string_view name, LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : Esp32SerialLinkTransport<decltype(Serial)>(Serial, UART_NUM_0, name,
                                                   sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
class ReliableSerial1 : public Esp32SerialLinkTransport<decltype(Serial1)> {
 public:
  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial1("serial1", sendbuf, recvbuf) {}

  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : Esp32SerialLinkTransport<decltype(Serial1)>(Serial1, UART_NUM_1, name,
                                                    sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
class ReliableSerial2 : public Esp32SerialLinkTransport<decltype(Serial2)> {
 public:
  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial2("serial2", sendbuf, recvbuf) {}

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