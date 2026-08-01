#pragma once

#if defined(ARDUINO_ARCH_RP2040)

#include "Arduino.h"
#include "roo_threads.h"
#include "roo_threads/atomic.h"
#include "roo_transport/link/arduino/link_stream_transport.h"

namespace roo_transport {
namespace rp2040 {

/// LinkStreamTransport variant that reads RP2040 UART data on a worker thread.
class ReliableUartLinkTransport : public LinkStreamTransport {
 public:
  /// Creates a transport over `serial` with receiver thread name `name`.
  ReliableUartLinkTransport(SerialUART& serial, roo::string_view name,
                            LinkBufferSize sendbuf = kBufferSize4KB,
                            LinkBufferSize recvbuf = kBufferSize4KB)
      : LinkStreamTransport(serial, sendbuf, recvbuf),
        serial_(serial),
        receiver_thread_name_(name) {}

  /// Creates a transport with a default receiver thread name.
  ReliableUartLinkTransport(SerialUART& serial,
                            LinkBufferSize sendbuf = kBufferSize4KB,
                            LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableUartLinkTransport(serial, "serialRcv", sendbuf, recvbuf) {}

  /// Starts the transport and the background receive thread.
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

  /// Stops the background receive thread.
  void end() {
    running_ = false;
    receiver_thread_.join();
  }

 private:
  SerialUART& serial_;
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

/// Reliable link transport bound to Arduino `Serial1` on RP2040.
class ReliableSerial1 : public ReliableUartLinkTransport {
 public:
  /// Creates a transport named `serial1`.
  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial1("serial1", sendbuf, recvbuf) {}

  /// Creates a transport with a custom diagnostic `name`.
  ReliableSerial1(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableUartLinkTransport(Serial1, name, sendbuf, recvbuf) {}
};

/// Reliable link transport bound to Arduino `Serial2` on RP2040.
class ReliableSerial2 : public ReliableUartLinkTransport {
 public:
  /// Creates a transport named `serial2`.
  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableSerial2("serial2", sendbuf, recvbuf) {}

  /// Creates a transport with a custom diagnostic `name`.
  ReliableSerial2(roo::string_view name,
                  LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableUartLinkTransport(Serial2, name, sendbuf, recvbuf) {}
};

}  // namespace rp2040
}  // namespace roo_transport

#endif  // defined(ARDUINO)