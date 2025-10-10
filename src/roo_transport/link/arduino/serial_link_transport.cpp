#if (defined ARDUINO)

#include "roo_transport/link/arduino/serial_link_transport.h"

#include "Arduino.h"

namespace roo_transport {

SerialLinkTransport::SerialLinkTransport(decltype(Serial1)& serial,
                                         LinkBufferSize sendbuf,
                                         LinkBufferSize recvbuf)
    : serial_(serial),
      output_(serial),
      input_(serial),
      sender_(output_),
      receiver_(input_),
      transport_(sender_, sendbuf, recvbuf) {}

void SerialLinkTransport::begin() {
  transport_.begin();
#if (defined ESP32 || defined ROO_TESTING)
  serial_.onReceive([this]() {
    receiver_.tryReceive([this](const roo::byte* buf, size_t len) {
      transport_.processIncomingPacket(buf, len);
    });
  });
#endif
}

// void SerialLinkTransport::loop() { transport_.loop(); }

LinkStream SerialLinkTransport::connectAsync() {
  return LinkStream(transport_.connect());
}

LinkStream SerialLinkTransport::connect() {
  LinkStream link = connectAsync();
  link.awaitConnected();
  return LinkStream(std::move(link));
}

}  // namespace roo_transport

#endif  // defined(ARDUINO)