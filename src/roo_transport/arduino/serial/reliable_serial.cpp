#include "roo_transport/arduino/serial/reliable_serial.h"

#include "Arduino.h"

namespace roo_io {

ReliableSerial::Connection::Connection(Channel& channel, uint32_t my_stream_id)
    : channel_(channel),
      my_stream_id_(my_stream_id),
      in_(channel_, my_stream_id),
      out_(channel_, my_stream_id) {}

ReliableSerial::ReliableSerial(decltype(Serial1)& serial,
                               unsigned int sendbuf_log2,
                               unsigned int recvbuf_log2, std::string label)
    : output_(serial),
      input_(serial),
      sender_(output_),
      receiver_(input_),
      channel_(sender_, receiver_, sendbuf_log2, recvbuf_log2) {
#ifdef ESP32
  serial.onReceive([this]() { channel_.tryRecv(); });
#endif
}

// void ReliableSerial::loop() { channel_.loop(); }

int ReliableSerial::Connection::available() { return in_.available(); }

int ReliableSerial::Connection::read() { return in_.read(); }

int ReliableSerial::Connection::peek() { return in_.peek(); }

size_t ReliableSerial::Connection::readBytes(char* buffer, size_t length) {
  return in_.timedRead((roo::byte*)buffer, length,
                       roo_time::Millis(getTimeout()));
}

size_t ReliableSerial::Connection::readBytes(uint8_t* buffer, size_t length) {
  return in_.timedRead((roo::byte*)buffer, length,
                       roo_time::Millis(getTimeout()));
}

size_t ReliableSerial::Connection::write(uint8_t val) {
  out().write((const roo::byte*)&val, 1);
  return 1;
}

size_t ReliableSerial::Connection::write(const uint8_t* buffer, size_t size) {
  return out().writeFully((const roo::byte*)buffer, size);
}

int ReliableSerial::Connection::availableForWrite() {
  return out_.availableForWrite();
}

void ReliableSerial::Connection::flush() { out().flush(); }

ReliableSerial::Connection* ReliableSerial::connectAsync() {
  uint32_t my_stream_id = channel_.connect();
  return new Connection(channel_, my_stream_id);
}

ReliableSerial::Connection* ReliableSerial::connect() {
  ReliableSerial::Connection* conn = connectAsync();
  conn->awaitConnected();
  return conn;
}

bool ReliableSerial::Connection::isConnecting() {
  return channel_.isConnecting(my_stream_id_);
}

void ReliableSerial::Connection::awaitConnected() {
  channel_.awaitConnected(my_stream_id_);
}

bool ReliableSerial::Connection::awaitConnected(roo_time::Interval timeout) {
  return channel_.awaitConnected(my_stream_id_, timeout);
}

}  // namespace roo_io
