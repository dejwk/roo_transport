#pragma once

#if defined(ARDUINO_ARCH_RP2040)

#include "Arduino.h"
#include "roo_threads.h"
#include "roo_threads/atomic.h"
#include "roo_transport/link/arduino/link_stream_transport.h"

namespace roo_transport {
namespace rp2040 {

// Implementation of the LinkStreamTransport that uses a newly created receiver
// thread to read from the underlying serial.
class ReliableUartLinkTransport : public LinkStreamTransport {
 public:
  ReliableUartLinkTransport(SerialUART &serial, roo::string_view name,
                            LinkBufferSize sendbuf = kBufferSize4KB,
                            LinkBufferSize recvbuf = kBufferSize4KB)
      : LinkStreamTransport(serial, sendbuf, recvbuf),
        serial_(serial),
        receiver_thread_name_(name) {}

  ReliableUartLinkTransport(SerialUART &serial,
                            LinkBufferSize sendbuf = kBufferSize4KB,
                            LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableUartLinkTransport(serial, "serialRcv", sendbuf, recvbuf) {}

  void begin() {
    LinkStreamTransport::begin();
    running_ = true;
    roo::thread::attributes attrs;
    attrs.set_name(receiver_thread_name_.c_str());
    // Run at high priority to ensure timely processing of incoming packets.
    attrs.set_priority(configMAX_PRIORITIES - 1);
    receiver_thread_ = roo::thread(attrs, [this]() {
      while (running_) {
        int avail = serial_.available();
        if (avail == 0) {
          // Don't busy-wait; give lower-priority tasks a chance to run.
          while (true) {
            roo::this_thread::sleep_for(roo_time::Millis(1));
            int avail = serial_.available();
            if (avail > 0) {
              break;
            }
          }
        }
        tryReceive();
      }
    });
  }

  void end() {
    running_ = false;
    receiver_thread_.join();
  }

 private:
  SerialUART &serial_;
  std::string receiver_thread_name_;
  roo::thread receiver_thread_;
  roo::atomic<bool> running_{false};
};

// class ReliableSerialTransport
//     : public Rp2040ReliableSerialTransport<decltype(Serial)> {
//  public:
//   ReliableSerialTransport(LinkBufferSize sendbuf = kBufferSize4KB,
//                           LinkBufferSize recvbuf = kBufferSize4KB)
//       : Rp2040SerialLinkTransport<decltype(Serial)>(Serial, sendbuf, recvbuf)
//       {}
// };

class ReliableSerial1 : public ReliableUartLinkTransport {
 public:
  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial1("serial1", sendbuf, recvbuf) {}

  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableUartLinkTransport(Serial1, name, sendbuf, recvbuf) {}
};

class ReliableSerial2 : public ReliableUartLinkTransport {
 public:
  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial2("serial2", sendbuf, recvbuf) {}

  ReliableSerial2(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableUartLinkTransport(Serial2, name, sendbuf, recvbuf) {}
};

}  // namespace rp2040
}  // namespace roo_transport

#endif  // defined(ARDUINO)