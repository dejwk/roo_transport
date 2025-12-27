#pragma once

#if defined(ARDUINO_ARCH_RP2040)

// Defines classes ReliableSerial, ReliableSerial1 and ReliableSerial2, which
// are meant as near-drop-in replacements for Serial, Serial1, and Serial2,
// respectively, that add a reliable transport layer on top of the underlying
// UART connection.

#include "Arduino.h"
#include "roo_io/core/input_stream.h"
#include "roo_transport/link/arduino/reliable_serial_transport.h"

namespace roo_transport {
namespace rp2040 {

class ReliableRp2040SerialUART : public LinkStream {
 public:
  ReliableRp2040SerialUART(SerialUART& serial, uint8_t num,
                           LinkBufferSize sendbuf = kBufferSize4KB,
                           LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableRp2040SerialUART(serial, num, "", sendbuf, recvbuf) {}

  ReliableRp2040SerialUART(SerialUART& serial, uint8_t num,
                           roo::string_view name,
                           LinkBufferSize sendbuf = kBufferSize4KB,
                           LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial), transport_(serial, name, sendbuf, recvbuf) {}

  void begin(unsigned long baud, uint32_t config = SERIAL_8N1) {
    serial_.begin(baud, config);
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

  bool setRX(pin_size_t pin) { return serial_.setRX(pin); }
  bool setTX(pin_size_t pin) { return serial_.setTX(pin); }
  bool setRTS(pin_size_t pin) { return serial_.setRTS(pin); }
  bool setCTS(pin_size_t pin) { return serial_.setCTS(pin); }

  bool setPinout(pin_size_t tx, pin_size_t rx) {
    return serial_.setPinout(tx, rx);
  }

  bool setInvertTX(bool invert = true) { return serial_.setInvertTX(invert); }
  bool setInvertRX(bool invert = true) { return serial_.setInvertRX(invert); }

  bool setInvertControl(bool invert = true) {
    return serial_.setInvertControl(invert);
  }

  bool setFIFOSize(size_t size) { return serial_.setFIFOSize(size); }
  bool setPollingMode(bool mode = true) { return serial_.setPollingMode(mode); }

 private:
  SerialUART& serial_;
  ReliableSerialTransport transport_;
};

class ReliableSerial1 : public ReliableRp2040SerialUART {
 public:
  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableRp2040SerialUART(Serial1, 1, name, sendbuf, recvbuf) {}

  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableRp2040SerialUART(Serial1, 1, "", sendbuf, recvbuf) {}
};

class ReliableSerial2 : public ReliableRp2040SerialUART {
 public:
  ReliableSerial2(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableRp2040SerialUART(Serial2, 2, name, sendbuf, recvbuf) {}

  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableRp2040SerialUART(Serial2, 2, "", sendbuf, recvbuf) {}
};

}  // namespace rp2040
}  // namespace roo_transport

#endif  // defined(ARDUINO_ARCH_RP2040)