#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_transport/link/arduino/serial_link_transport.h"

namespace roo_transport {

#if (defined ESP32 || defined ROO_TESTING)

template <typename SerialType>
class ReliableHardwareSerial : public LinkStream {
 public:
  ReliableHardwareSerial(SerialType& serial,
                         LinkBufferSize sendbuf = kBufferSize4KB,
                         LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial), transport_(serial, sendbuf, recvbuf) {}

  void begin(unsigned long baud, uint32_t config = SERIAL_8N1,
             int8_t rxPin = -1, int8_t txPin = -1, bool invert = false,
             unsigned long timeout_ms = 20000UL,
             uint8_t rxfifo_full_thrhd = 112) {
    serial_.begin(baud, config, rxPin, txPin, invert, timeout_ms,
                  rxfifo_full_thrhd);
    transport_.begin();
    set(transport_.transport().connect([]() {
      LOG(FATAL) << "Reliable serial peer reset detected; rebooting";
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

class ReliableSerial : public ReliableHardwareSerial<decltype(Serial)> {
 public:
  ReliableSerial(LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableHardwareSerial(Serial, sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
class ReliableSerial1 : public ReliableHardwareSerial<decltype(Serial1)> {
 public:
  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableHardwareSerial(Serial1, sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
class ReliableSerial2 : public ReliableHardwareSerial<decltype(Serial2)> {
 public:
  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableHardwareSerial(Serial2, sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 2

#endif  // ESP32

}  // namespace roo_transport

#endif  // defined(ARDUINO)