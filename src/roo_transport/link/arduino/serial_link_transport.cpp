#if (defined ARDUINO)

#include "roo_transport/link/arduino/serial_link_transport.h"

#include "Arduino.h"

namespace roo_transport {

SerialLinkTransport::SerialLinkTransport(decltype(Serial1)& serial,
                                         unsigned int sendbuf_log2,
                                         unsigned int recvbuf_log2,
                                         std::string label)
    : serial_(serial),
      output_(serial),
      input_(serial),
      sender_(output_),
      receiver_(input_),
      transport_(sender_, receiver_, sendbuf_log2, recvbuf_log2) {}

void SerialLinkTransport::begin() {
  transport_.begin();
#ifdef ESP32
  serial_.onReceive([this]() { transport_.tryReceive(); });
#endif
}

void SerialLinkTransport::loop() { transport_.loop(); }

SerialLink SerialLinkTransport::connectAsync() {
  return SerialLink(transport_.connect());
}

SerialLink SerialLinkTransport::connect() {
  SerialLink link = connectAsync();
  link.awaitConnected();
  return SerialLink(std::move(link));
}

}  // namespace roo_transport

#endif  // defined(ARDUINO)