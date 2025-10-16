#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_io/core/input_stream.h"
#include "roo_transport/link/arduino/serial_link_transport.h"

#if (defined ESP32 || defined ROO_TESTING)
#include "roo_io/uart/esp32/uart_input_stream.h"
#include "roo_io/uart/esp32/uart_output_stream.h"
#endif

namespace roo_transport {

#if (defined ESP32 || defined ROO_TESTING)

template <typename SerialType>
class ReliableEsp32Serial : public LinkStream {
 public:
  ReliableEsp32Serial(SerialType& serial, uint8_t num,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(serial, num, "", sendbuf, recvbuf) {}

  ReliableEsp32Serial(SerialType& serial, uint8_t num, roo::string_view name,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial), transport_(serial, name, sendbuf, recvbuf) {}

  void begin(unsigned long baud, uint32_t config = SERIAL_8N1,
             int8_t rxPin = -1, int8_t txPin = -1, bool invert = false,
             unsigned long timeout_ms = 20000UL,
             uint8_t rxfifo_full_thrhd = 112) {
    serial_.setRxBufferSize(4096);
    serial_.begin(baud, config, rxPin, txPin, invert, timeout_ms,
                  rxfifo_full_thrhd);
    transport_.begin();
    set(transport_.transport().connect([]() {
      LOG(FATAL) << "Reliable serial: peer reset detected; rebooting";
    }));
  }

  void end() {
    out().close();
    in().close();
    serial_.end();
  }

 private:
  SerialType& serial_;
  SerialLinkTransport<SerialType> transport_;
};

// Specialization for HardwareSerial that uses more efficient UART streams
// (directly using esp-idf UART driver).
template <>
class ReliableEsp32Serial<HardwareSerial> : public LinkStream {
 public:
  ReliableEsp32Serial(HardwareSerial& serial, uint8_t num,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(serial, num, "", sendbuf, recvbuf) {}

  ReliableEsp32Serial(HardwareSerial& serial, uint8_t num,
                      roo::string_view name,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial),
        num_(num),
        output_((uart_port_t)num),
        input_((uart_port_t)num),
        sender_(output_),
        receiver_(input_),
        transport_(sender_, name, sendbuf, recvbuf),
        process_fn_([this](const roo::byte* buf, size_t len) {
          transport_.processIncomingPacket(buf, len);
        }) {}

  bool setPins(int8_t rxPin, int8_t txPin, int8_t ctsPin = -1,
               int8_t rtsPin = -1) {
    return serial_.setPins(rxPin, txPin, ctsPin, rtsPin);
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

  void begin() { begin(5000000, SERIAL_8N1); }

  void begin(unsigned long baud, uint32_t config = SERIAL_8N1,
             int8_t rxPin = -1, int8_t txPin = -1, bool invert = false,
             unsigned long timeout_ms = 20000UL,
             uint8_t rxfifo_full_thrhd = 100) {
    serial_.setRxBufferSize(4096);
    serial_.begin(baud, config, rxPin, txPin, invert, timeout_ms,
                  rxfifo_full_thrhd);
    transport_.begin();
    serial_.onReceive([this]() { receiver_.tryReceive(process_fn_); });
    serial_.onReceiveError(
        [this](hardwareSerial_error_t) { receiver_.tryReceive(process_fn_); });
    set(transport_.connect([]() {
      LOG(FATAL) << "Reliable serial: peer reset detected; rebooting";
    }));
  }

  void end() {
    out().close();
    in().close();
    serial_.onReceive(nullptr);
    serial_.end();
  }

 private:
  HardwareSerial& serial_;
  uint8_t num_;
  roo_io::Esp32UartOutputStream output_;
  roo_io::Esp32UartInputStream input_;
  PacketSenderOverStream sender_;
  PacketReceiverOverStream receiver_;

  LinkTransport transport_;

  std::function<void(const roo::byte* buf, size_t len)> process_fn_;
};

class ReliableSerial : public ReliableEsp32Serial<decltype(Serial)> {
 public:
  ReliableSerial(roo::string_view name, LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial, 0, name, sendbuf, recvbuf) {}

  ReliableSerial(LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial, 0, "", sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
class ReliableSerial1 : public ReliableEsp32Serial<decltype(Serial1)> {
 public:
  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial1, 1, name, sendbuf, recvbuf) {}

  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial1, 1, "", sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
class ReliableSerial2 : public ReliableEsp32Serial<decltype(Serial2)> {
 public:
  ReliableSerial2(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial2, 2, name, sendbuf, recvbuf) {}

  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial2, 2, "", sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 2

#endif  // ESP32

}  // namespace roo_transport

#endif  // defined(ARDUINO)