#ifdef ARDUINO

#include "roo_transport/singleton_socket/arduino/singleton_serial_socket.h"

#include <cstddef>

namespace roo_transport {

SingletonSerialSocket::SingletonSerialSocket() : socket_() {}

SingletonSerialSocket::SingletonSerialSocket(Channel& channel,
                                             uint32_t my_stream_id)
    : socket_(channel, my_stream_id) {}

SingletonSerialSocket::SingletonSerialSocket(SingletonSocket socket)
    : socket_(std::move(socket)) {}

int SingletonSerialSocket::available() { return in().available(); }

int SingletonSerialSocket::read() { return in().read(); }

int SingletonSerialSocket::peek() { return in().peek(); }

size_t SingletonSerialSocket::readBytes(char* buffer, size_t length) {
  return timedRead((roo::byte*)buffer, length, roo_time::Millis(getTimeout()));
}

size_t SingletonSerialSocket::readBytes(uint8_t* buffer, size_t length) {
  return timedRead((roo::byte*)buffer, length, roo_time::Millis(getTimeout()));
}

size_t SingletonSerialSocket::write(uint8_t val) {
  out().write((const roo::byte*)&val, 1);
  return 1;
}

size_t SingletonSerialSocket::write(const uint8_t* buffer, size_t size) {
  return out().writeFully((const roo::byte*)buffer, size);
}

int SingletonSerialSocket::availableForWrite() {
  return out().availableForWrite();
}

void SingletonSerialSocket::flush() { out().flush(); }

bool SingletonSerialSocket::isConnecting() { return socket_.isConnecting(); }

void SingletonSerialSocket::awaitConnected() { socket_.awaitConnected(); }

bool SingletonSerialSocket::awaitConnected(roo_time::Interval timeout) {
  return socket_.awaitConnected(timeout);
}

size_t SingletonSerialSocket::timedRead(roo::byte* buf, size_t count,
                                        roo_time::Interval timeout) {
  roo_time::Uptime start = roo_time::Uptime::Now();
  size_t total = 0;
  if (in().status() != roo_io::kOk) return -1;
  while (count > 0) {
    for (int i = 0; i < 100; ++i) {
      size_t result = in().tryRead(buf, count);
      if (result == 0) {
        if (in().status() != roo_io::kOk) return -1;
        socket_.channel_->loop();
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
