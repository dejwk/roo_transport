#pragma once

#if (defined ARDUINO)
#if (defined ESP32 || defined ROO_TESTING)

// Defines classes ReliableSerial, and (depending on the exact SOC also
// ReliableSerial1 and ReliableSerial2, which are meant as near-drop-in
// replacements for Serial, Serial1, and Serial2, respectively, that add a
// reliable transport layer on top of the underlying UART connection.

#include "Arduino.h"
#include "roo_io/core/input_stream.h"
#include "roo_io/uart/esp32/uart_input_stream.h"
#include "roo_io/uart/esp32/uart_output_stream.h"
#include "roo_transport/link/arduino/esp32/reliable_serial_transport.h"

namespace roo_transport {
namespace esp32 {

template <typename SerialType>
class ReliableEsp32Serial : public LinkStream {
 public:
  ReliableEsp32Serial(SerialType& serial, uart_port_t num,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(serial, num, "", sendbuf, recvbuf) {}

  ReliableEsp32Serial(SerialType& serial, uart_port_t num,
                      roo::string_view name,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial), transport_(serial, num, name, sendbuf, recvbuf) {}

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
  Esp32SerialLinkTransport<SerialType> transport_;
};

class ReliableSerial : public ReliableEsp32Serial<decltype(Serial)> {
 public:
  ReliableSerial(roo::string_view name, LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial, UART_NUM_0, name, sendbuf, recvbuf) {}

  ReliableSerial(LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial, UART_NUM_0, "serial", sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
class ReliableSerial1 : public ReliableEsp32Serial<decltype(Serial1)> {
 public:
  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial1, UART_NUM_1, name, sendbuf, recvbuf) {}

  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial1, UART_NUM_1, "serial1", sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
class ReliableSerial2 : public ReliableEsp32Serial<decltype(Serial2)> {
 public:
  ReliableSerial2(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial2, UART_NUM_2, name, sendbuf, recvbuf) {}

  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial2, UART_NUM_2, "serial2", sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 2

}  // namespace esp32
}  // namespace roo_transport

#endif  // ESP32 || ROO_TESTING
#endif  // defined(ARDUINO)
