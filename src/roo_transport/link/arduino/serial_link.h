#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_transport/link/link.h"

namespace roo_transport {

class SerialLink : public Stream {
 public:
  SerialLink();

  SerialLink(Channel& channel, uint32_t my_stream_id);

  // Decorate a non-Arduino link into an Arduino-compliant one.
  SerialLink(Link link);

  // Status status() const;

  int available() override;
  int read() override;
  int peek() override;

  size_t readBytes(char* buffer, size_t length) override;
  size_t readBytes(uint8_t* buffer, size_t length) override;

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int availableForWrite() override;
  void flush() override;

  // Obtains the input stream that can be used to read from the reliable
  // serial.
  SocketInputStream& in() { return link_.in(); }

  // Obtains the output stream that can be used to write to the reliable
  // serial.
  SocketOutputStream& out() { return link_.out(); }

  bool isConnecting();

  void awaitConnected();

  bool awaitConnected(roo_time::Interval timeout);

 private:
  size_t timedRead(roo::byte* buf, size_t count, roo_time::Interval timeout);

  Link link_;
};

}  // namespace roo_transport

#endif  // defined(ARDUINO)