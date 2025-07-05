#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_transport/singleton_socket/internal/thread_safe/channel_input.h"
#include "roo_transport/singleton_socket/internal/thread_safe/channel_output.h"
#include "roo_transport/singleton_socket/singleton_socket.h"

namespace roo_io {

class SingletonSerialSocket : public Stream {
 public:
  SingletonSerialSocket();

  SingletonSerialSocket(Channel& channel, uint32_t my_stream_id);

  // Decorate a non-Arduino singleton socket into an Arduino-compliant one.
  SingletonSerialSocket(SingletonSocket socket);

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
  roo_io::ChannelInput& in() { return socket_.in(); }

  // Obtains the output stream that can be used to write to the reliable
  // serial.
  roo_io::ChannelOutput& out() { return socket_.out(); }

  bool isConnecting();

  void awaitConnected();

  bool awaitConnected(roo_time::Interval timeout);

 private:
  SingletonSocket socket_;
};

}  // namespace roo_io

#endif  // defined(ARDUINO)