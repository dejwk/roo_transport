#ifdef ARDUINO

#include "roo_transport/link/arduino/serial_link.h"

#include <cstddef>

namespace roo_transport {

SerialLink::SerialLink() : link_() {}

SerialLink::SerialLink(Channel& channel, uint32_t my_stream_id)
    : link_(channel, my_stream_id) {}

SerialLink::SerialLink(Link socket) : link_(std::move(socket)) {}

int SerialLink::available() { return in().available(); }

int SerialLink::read() { return in().read(); }

int SerialLink::peek() { return in().peek(); }

size_t SerialLink::readBytes(char* buffer, size_t length) {
  return timedRead((roo::byte*)buffer, length, roo_time::Millis(getTimeout()));
}

size_t SerialLink::readBytes(uint8_t* buffer, size_t length) {
  return timedRead((roo::byte*)buffer, length, roo_time::Millis(getTimeout()));
}

size_t SerialLink::write(uint8_t val) {
  out().write((const roo::byte*)&val, 1);
  return 1;
}

size_t SerialLink::write(const uint8_t* buffer, size_t size) {
  return out().writeFully((const roo::byte*)buffer, size);
}

int SerialLink::availableForWrite() { return out().availableForWrite(); }

void SerialLink::flush() { out().flush(); }

LinkStatus SerialLink::status() const { return link_.status(); }

void SerialLink::awaitConnected() { link_.awaitConnected(); }

bool SerialLink::awaitConnected(roo_time::Interval timeout) {
  return link_.awaitConnected(timeout);
}

size_t SerialLink::timedRead(roo::byte* buf, size_t count,
                             roo_time::Interval timeout) {
  roo_time::Uptime start = roo_time::Uptime::Now();
  size_t total = 0;
  if (in().status() != roo_io::kOk) return -1;
  while (count > 0) {
    for (int i = 0; i < 100; ++i) {
      size_t result = in().tryRead(buf, count);
      if (result == 0) {
        if (in().status() != roo_io::kOk) return -1;
        link_.channel_->loop();
      } else {
        total += result;
        count -= result;
      }
      if (count == 0) return total;
    }
    if (roo_time::Uptime::Now() - start > timeout) break;
    delay(1);
  }
  return total;
}

}  // namespace roo_transport

#endif
