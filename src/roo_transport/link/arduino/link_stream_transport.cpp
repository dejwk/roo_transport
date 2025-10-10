#if (defined ARDUINO)

#include "roo_transport/link/arduino/link_stream_transport.h"

#include "Arduino.h"

namespace roo_transport {

LinkStreamTransport::LinkStreamTransport(Stream& stream, LinkBufferSize sendbuf,
                                         LinkBufferSize recvbuf)
    : stream_(stream),
      output_(stream_),
      input_(stream_),
      sender_(output_),
      receiver_(input_),
      transport_(sender_, sendbuf, recvbuf) {}

void LinkStreamTransport::begin() { transport_.begin(); }

LinkStream LinkStreamTransport::connectAsync(
    std::function<void()> disconnect_fn) {
  return LinkStream(transport_.connect(std::move(disconnect_fn)));
}

LinkStream LinkStreamTransport::connect(std::function<void()> disconnect_fn) {
  LinkStream link = connectAsync(std::move(disconnect_fn));
  link.awaitConnected();
  return LinkStream(std::move(link));
}

size_t LinkStreamTransport::tryReceive() {
  return receiver_.tryReceive([this](const roo::byte* buf, size_t len) {
    transport_.processIncomingPacket(buf, len);
  });
}

size_t LinkStreamTransport::receive() {
  return receiver_.receive([this](const roo::byte* buf, size_t len) {
    transport_.processIncomingPacket(buf, len);
  });
}

}  // namespace roo_transport

#endif  // defined(ARDUINO)