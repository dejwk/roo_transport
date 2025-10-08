#include "roo_transport/link/arduino/serial_link_transport.h"

#include "Arduino.h"
#include "Stream.h"
#include "gtest/gtest.h"
#include "roo_io/ringpipe/ringpipe.h"
#include "roo_testing/devices/microcontroller/esp32/fake_esp32.h"

class UartEndpoint : public FakeUartDevice {
 public:
  UartEndpoint() : FakeUartDevice(), rx_(120), tx_(120) {}

  size_t read(uint8_t* buf, uint16_t size) override {
    return rx_.read((roo::byte*)buf, size);
  }

  size_t write(const uint8_t* buf, uint16_t size) override {
    return tx_.write((const roo::byte*)buf, size);
  }

  size_t availableForRead() override { return rx_.availableForRead(); }

  size_t availableForWrite() override { return tx_.availableForWrite(); }

  roo_io::RingPipe& rx() { return rx_; }
  roo_io::RingPipe& tx() { return tx_; }

 private:
  roo_io::RingPipe rx_;
  roo_io::RingPipe tx_;
};

class UartTransmitter {
 public:
  UartTransmitter(roo_io::RingPipe& source, roo_io::RingPipe& target,
                  UartEndpoint& target_endpoint)
      : source_(source), target_(target), target_endpoint_(target_endpoint) {
    roo::thread::attributes attrs;
    attrs.set_name("UartTx");
    attrs.set_priority(configMAX_PRIORITIES - 1);
    thread_ = roo::thread(attrs, [this]() { run(); });
  }

  ~UartTransmitter() {
    source_.closeInput();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void run() {
    roo::byte buffer[128];
    while (true) {
      size_t len = source_.read(buffer, 128);
      const roo::byte* ptr = buffer;
      if (len == 0) break;
      while (len > 0) {
        size_t written = target_.write(ptr, len);
        target_endpoint_.notifyDataAvailable();
        if (written == 0) break;
        ptr += written;
        len -= written;
      }
    }
  }

 private:
  roo_io::RingPipe& source_;
  roo_io::RingPipe& target_;
  UartEndpoint& target_endpoint_;
  roo::thread thread_;
  std::atomic<bool> closed_{false};
};

class UartLink {
 public:
  UartLink(UartEndpoint& endpoint1, UartEndpoint& endpoint2)
      : endpoint1_(endpoint1),
        endpoint2_(endpoint2),
        transmitter1_(endpoint1_.tx(), endpoint2_.rx(), endpoint2_),
        transmitter2_(endpoint2_.tx(), endpoint1_.rx(), endpoint1_) {}

  ~UartLink() {
    endpoint1_.tx().closeOutput();
    endpoint2_.tx().closeOutput();
  }

 private:
  UartEndpoint& endpoint1_;
  UartEndpoint& endpoint2_;
  UartTransmitter transmitter1_;
  UartTransmitter transmitter2_;
};

struct Emulator {
  UartEndpoint peer1;
  UartEndpoint peer2;
  UartLink uart_link;

  Emulator() : peer1(), peer2(), uart_link(peer1, peer2) {
    auto& board = FakeEsp32();
    board.attachUartDevice(peer1, 13, 12);
    board.attachUartDevice(peer2, 15, 14);
  }
} emulator;

namespace roo_transport {

TEST(ReliableSerial, ConstructionDestruction) {
  SerialLinkTransport transport(Serial1);
  SerialLink link;
  EXPECT_EQ(link.status(), LinkStatus::kIdle);
}

class TransferTest : public ::testing::Test {
 protected:
  TransferTest() : transport1(Serial1), transport2(Serial2) {
    Serial1.begin(1000000, SERIAL_8N1, 12, 13);
    Serial2.begin(1000000, SERIAL_8N1, 14, 15);

    transport1.begin();
    transport2.begin();
  }

  ~TransferTest() {
    join();
    Serial1.end();
    Serial2.end();
  }

  void join() {
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    if (client_thread_.joinable()) {
      client_thread_.join();
    }
  }

  void server(std::function<void(SerialLink& stream)> fn) {
    roo::thread::attributes server_attrs;
    server_attrs.set_name("server");
    server_thread_ = roo::thread(server_attrs, [this, fn]() {
      SerialLink server = transport1.connect();
      ASSERT_EQ(server.status(), LinkStatus::kConnected);
      ASSERT_EQ(server.in().status(), roo_io::kOk);
      ASSERT_EQ(server.out().status(), roo_io::kOk);
      fn(server);
      server.out().close();
      ASSERT_EQ(server.out().status(), roo_io::kClosed);
    });
  }

  void client(std::function<void(SerialLink& stream)> fn) {
    roo::thread::attributes client_attrs;
    client_attrs.set_name("client");
    client_thread_ = roo::thread(client_attrs, [this, fn]() {
      SerialLink client = transport2.connect();
      ASSERT_EQ(client.status(), LinkStatus::kConnected);
      ASSERT_EQ(client.in().status(), roo_io::kOk);
      ASSERT_EQ(client.out().status(), roo_io::kOk);
      fn(client);
      client.out().close();
      ASSERT_EQ(client.out().status(), roo_io::kClosed);
    });
  }

  SerialLinkTransport transport1;
  SerialLinkTransport transport2;

  roo::thread server_thread_;
  roo::thread client_thread_;
};

TEST_F(TransferTest, TransferString) {
  // NOTE: readString() is horrible; it depends on timing, and you should
  // generally not use it for real applications.
  server([](Stream& stream) { stream.print("Hello"); });
  client([](Stream& stream) {
    EXPECT_STREQ("Hello", stream.readString().c_str());
  });
}

TEST_F(TransferTest, TransferByteByByte) {
  server([](Stream& stream) {
    stream.write('H');
    stream.write('e');
    stream.write('l');
    stream.write('l');
    stream.write('o');
  });
  client([](Stream& stream) {
    while (stream.available() == 0) {
      delay(1);
    }
    EXPECT_EQ('H', stream.read());
    while (stream.available() == 0) {
      delay(1);
    }
    EXPECT_EQ('e', stream.read());
    while (stream.available() == 0) {
      delay(1);
    }
    EXPECT_EQ('l', stream.read());
    while (stream.available() == 0) {
      delay(1);
    }
    EXPECT_EQ('l', stream.read());
    while (stream.available() == 0) {
      delay(1);
    }
    EXPECT_EQ('o', stream.read());
  });
}

// Using blocking I/O (the preferred way).
TEST_F(TransferTest, BlockingTransfer) {
  server([](SerialLink& stream) {
    roo::byte buf[64];
    EXPECT_EQ(13, stream.in().readFully(buf, 13));
    buf[13] = roo::byte{0};
    EXPECT_STREQ("Hello, world!", (const char*)buf);
    stream.out().writeFully((const roo::byte*)"Hello, ", 7);
    stream.out().writeFully((const roo::byte*)"back!", 5);
  });
  client([](SerialLink& stream) {
    stream.out().writeFully((const roo::byte*)"Hello, ", 7);
    stream.out().writeFully((const roo::byte*)"world!", 6);
    stream.out().flush();
    roo::byte buf[64];
    EXPECT_EQ(12, stream.in().readFully(buf, 12));
    buf[12] = roo::byte{0};
    EXPECT_STREQ("Hello, back!", (const char*)buf);
  });
}

}  // namespace roo_transport