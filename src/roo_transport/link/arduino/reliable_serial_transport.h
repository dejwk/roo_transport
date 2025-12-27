#pragma once

#if (defined ARDUINO)

#if (defined ESP32 || defined ROO_TESTING)
#include "roo_transport/link/arduino/esp32/reliable_serial_transport.h"

namespace roo_transport {
using esp32::ReliableSerialTransport;
#if SOC_UART_NUM > 1
using esp32::ReliableSerial1Transport;
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
using esp32::ReliableSerial2Transport;
#endif  // SOC_UART_NUM > 2
}  // namespace roo_transport

#else  // not ESP32

#include "Arduino.h"
#include "roo_threads.h"
#include "roo_threads/thread.h"
#include "roo_transport/link/arduino/link_stream_transport.h"

namespace roo_transport {

// Implementation of the LinkStreamTransport that uses a newly created receiver
// thread to read from the underlying serial.
class ReliableSerialTransport : public LinkStreamTransport {
 public:
  ReliableSerialTransport(Stream& serial, roo::string_view name,
                             LinkBufferSize sendbuf = kBufferSize4KB,
                             LinkBufferSize recvbuf = kBufferSize4KB)
      : LinkStreamTransport(serial, sendbuf, recvbuf),
        receiver_thread_name_(name) {}

  ReliableSerialTransport(Stream& serial,
                             LinkBufferSize sendbuf = kBufferSize4KB,
                             LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerialTransport(serial, "serialRcv", sendbuf, recvbuf) {}

  void begin() {
    LinkStreamTransport::begin();
    running_ = true;
    roo::thread::attributes attrs;
#if ROO_THREADS_ATTRIBUTES_SUPPORT_NAME
    attrs.set_name(receiver_thread_name_.c_str());
#endif
#if ROO_THREADS_ATTRIBUTES_SUPPORT_PRIORITY
    attrs.set_priority(2);
#endif
    receiver_thread_ = roo::thread(attrs, [this]() {
      while (running_) {
        this->receive();
      }
    });
  }

  void end() {
    running_ = false;
    receiver_thread_.join();
  }

 private:
  std::string receiver_thread_name_;
  roo::thread receiver_thread_;
  roo::atomic<bool> running_{false};
};

#endif

}  // namespace roo_transport

#endif  // defined(ARDUINO)