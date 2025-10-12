#pragma once

#if (defined ARDUINO)

#include "Arduino.h"
#include "roo_io/core/input_stream.h"
#include "roo_transport/link/arduino/serial_link_transport.h"

#if (defined ESP32 || defined ROO_TESTING)
#include "HardwareSerial.h"
#include "driver/uart.h"
#endif

namespace roo_transport {

#if (defined ESP32 || defined ROO_TESTING)

template <typename SerialType>
class ReliableEsp32Serial : public LinkStream {
 public:
  ReliableEsp32Serial(SerialType& serial,
                      LinkBufferSize sendbuf = kBufferSize4KB,
                      LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial), transport_(serial, sendbuf, recvbuf) {}

  void begin(unsigned long baud, uint32_t config = SERIAL_8N1,
             int8_t rxPin = -1, int8_t txPin = -1, bool invert = false,
             unsigned long timeout_ms = 20000UL,
             uint8_t rxfifo_full_thrhd = 112) {
    serial_.setRxBufferSize(512);
    serial_.begin(baud, config, rxPin, txPin, invert, timeout_ms,
                  rxfifo_full_thrhd);
    transport_.begin();
    set(transport_.transport().connect([]() {
      LOG(FATAL) << "Reliable serial: peer reset detected; rebooting";
    }));
  }

  void end() {
    out().close();
    in().close();
    serial_.end();
  }

 private:
  SerialType& serial_;
  SerialLinkTransport<SerialType> transport_;
};

class ReliableHardwareSerial : public LinkStream {
 public:
  class UartInputStream : public roo_io::InputStream {
   public:
    UartInputStream(int num) : num_(num) {}

    size_t tryRead(roo::byte* buf, size_t count) override {
      if (!isOpen()) return 0;
      int read = uart_read_bytes((uart_port_t)num_, buf, count, 0);
      return read > 0 ? read : 0;
    }

    size_t read(roo::byte* buf, size_t count) override {
      if (!isOpen() || count == 0) return 0;
      while (true) {
        int read = uart_read_bytes((uart_port_t)num_, buf, count, 0);
        if (read > 0) return read;
        read = uart_read_bytes((uart_port_t)num_, buf, 1, portMAX_DELAY);
        if (read > 0) return read;
        status_ = roo_io::kReadError;
        return 0;
      }
    }

    size_t readFully(roo::byte* buf, size_t count) override {
      if (!isOpen() || count == 0) return 0;
      size_t total = 0;
      while (total < count) {
        int read =
            uart_read_bytes((uart_port_t)num_, buf, count, portMAX_DELAY);
        if (read < 0) {
          status_ = roo_io::kReadError;
          break;
        }
        total += read;
        buf += read;
        count -= read;
      }
      return total;
    }

    bool isOpen() const override { return status() == roo_io::kOk; }

    void close() override { status_ = roo_io::kClosed; }

    roo_io::Status status() const override { return status_; }

   private:
    int num_;
    mutable roo_io::Status status_;
  };

  ReliableHardwareSerial(HardwareSerial& serial, int num,
                         LinkBufferSize sendbuf = kBufferSize4KB,
                         LinkBufferSize recvbuf = kBufferSize4KB)
      : serial_(serial),
        num_(num),
        output_(serial_),
        input_(num),
        sender_(output_),
        receiver_(input_),
        transport_(sender_, sendbuf, recvbuf),
        process_fn_([this](const roo::byte* buf, size_t len) {
          transport_.processIncomingPacket(buf, len);
        }) {}

  LinkStream connect(std::function<void()> disconnect_fn = nullptr) {
    LinkStream link = connectAsync(std::move(disconnect_fn));
    link.awaitConnected();
    return LinkStream(std::move(link));
  }

  LinkStream connectAsync(std::function<void()> disconnect_fn = nullptr) {
    return LinkStream(transport_.connect(std::move(disconnect_fn)));
  }

  uint32_t packets_sent() const { return transport_.packets_sent(); }
  uint32_t packets_delivered() const { return transport_.packets_delivered(); }
  uint32_t packets_received() const { return transport_.packets_received(); }

  size_t receiver_bytes_received() const { return receiver_.bytes_received(); }
  size_t receiver_bytes_accepted() const { return receiver_.bytes_accepted(); }

  LinkTransport& transport() { return transport_; }

  void begin(unsigned long baud, uint32_t config = SERIAL_8N1,
             int8_t rxPin = -1, int8_t txPin = -1, bool invert = false,
             unsigned long timeout_ms = 20000UL,
             uint8_t rxfifo_full_thrhd = 100) {
    serial_.setRxBufferSize(4096);
    serial_.begin(baud, config, rxPin, txPin, invert, timeout_ms,
                  rxfifo_full_thrhd);
    transport_.begin();
    serial_.onReceive([this]() {
      receiver_.tryReceive(process_fn_);
    });
    serial_.onReceiveError([this](hardwareSerial_error_t) {
      receiver_.tryReceive(process_fn_);
    });
    set(transport_.connect([]() {
      LOG(FATAL) << "Reliable serial: peer reset detected; rebooting";
    }));
  }

  void end() {
    out().close();
    in().close();
    serial_.onReceive(nullptr);
    serial_.end();
  }

 private:
  HardwareSerial& serial_;
  int num_;
  roo_io::ArduinoSerialOutputStream<HardwareSerial> output_;
  // roo_io::ArduinoSerialInputStream<HardwareSerial> input_;
  UartInputStream input_;
  PacketSenderOverStream sender_;
  PacketReceiverOverStream receiver_;

  LinkTransport transport_;

  std::function<void(const roo::byte* buf, size_t len)> process_fn_;
};

class ReliableSerial : public ReliableEsp32Serial<decltype(Serial)> {
 public:
  ReliableSerial(LinkBufferSize sendbuf = kBufferSize4KB,
                 LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableEsp32Serial(Serial, sendbuf, recvbuf) {}
};

#if SOC_UART_NUM > 1
class ReliableSerial1 : public ReliableHardwareSerial {
 public:
  ReliableSerial1(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableHardwareSerial(Serial1, 1, sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 1
#if SOC_UART_NUM > 2
class ReliableSerial2 : public ReliableHardwareSerial {
 public:
  ReliableSerial2(LinkBufferSize sendbuf = kBufferSize4KB,
                  LinkBufferSize recvbuf = kBufferSize4KB)
      : ReliableHardwareSerial(Serial2, 2, sendbuf, recvbuf) {}
};
#endif  // SOC_UART_NUM > 2

#endif  // ESP32

}  // namespace roo_transport

#endif  // defined(ARDUINO)