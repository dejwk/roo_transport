#pragma once

#if (defined ARDUINO)

#include "roo_transport/link/arduino/link_stream_transport.h"

namespace roo_transport {

#if (defined ESP32 || defined ROO_TESTING)

class SerialLinkTransport : public LinkStreamTransport {
 public:
  SerialLinkTransport(HardwareSerial& serial,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : LinkStreamTransport(serial, sendbuf, recvbuf), serial_(serial) {}

  void begin() {
    LinkStreamTransport::begin();
    serial_.onReceive([this]() { tryReceive(); });
  }

  void end() {
    serial_.onReceive(nullptr);
    serial_.end();
  }

 private:
  HardwareSerial& serial_;
};

#endif  // ESP32

}  // namespace roo_transport

#endif  // defined(ARDUINO)