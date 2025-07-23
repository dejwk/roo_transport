#if (defined ARDUINO)

#include "roo_transport/singleton_socket/arduino/singleton_serial.h"

#include "Arduino.h"

namespace roo_transport {

SingletonSerial::SingletonSerial(decltype(Serial1)& serial,
                                 unsigned int sendbuf_log2,
                                 unsigned int recvbuf_log2, std::string label)
    : serial_(serial),
      output_(serial),
      input_(serial),
      sender_(output_),
      receiver_(input_),
      transport_(sender_, receiver_, sendbuf_log2, recvbuf_log2) {}

void SingletonSerial::begin() {
  transport_.begin();
#ifdef ESP32
  serial_.onReceive([this]() { transport_.readData(); });
#endif
}

void SingletonSerial::loop() { transport_.loop(); }

SingletonSerialSocket SingletonSerial::connectAsync() {
  return SingletonSerialSocket(transport_.connect());
}

SingletonSerialSocket SingletonSerial::connect() {
  SingletonSerialSocket socket = connectAsync();
  socket.awaitConnected();
  return SingletonSerialSocket(std::move(socket));
}

}  // namespace roo_transport

#endif  // defined(ARDUINO)