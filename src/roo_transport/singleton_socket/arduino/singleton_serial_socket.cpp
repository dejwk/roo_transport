#ifdef ARDUINO

#include "roo_transport/singleton_socket/arduino/singleton_serial_socket.h"

namespace roo_io {

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
  return in().timedRead((roo::byte*)buffer, length,
                        roo_time::Millis(getTimeout()));
}

size_t SingletonSerialSocket::readBytes(uint8_t* buffer, size_t length) {
  return in().timedRead((roo::byte*)buffer, length,
                        roo_time::Millis(getTimeout()));
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

}  // namespace roo_io

#endif
