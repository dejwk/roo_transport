#ifdef ROO_TESTING

// This section is intended for testing the example on Linux. You can disregard
// it when analyzing the example itself - just scroll down to the #endif.

#include "roo_io/ringpipe/ringpipe.h"
#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"

class FakeUartEndpoint : public FakeUartDevice {
 public:
  FakeUartEndpoint() : tx_(256), rx_(256) {}

  size_t write(const uint8_t* buf, uint16_t size) override {
    return tx_.writeFully((const roo::byte*)buf, size);
  }

  size_t read(uint8_t* buf, uint16_t size) override {
    return rx_.tryRead((roo::byte*)buf, size);
  }

  size_t availableForRead() override { return rx_.availableForRead(); }

  size_t availableForWrite() override { return tx_.availableForWrite(); }

  roo_io::RingPipe& tx() { return tx_; }
  roo_io::RingPipe& rx() { return rx_; }

 private:
  roo_io::RingPipe tx_;
  roo_io::RingPipe rx_;
};

class UartForwarder {
 public:
  UartForwarder(roo_io::RingPipe& from, roo_io::RingPipe& to,
                FakeUartDevice& recv)
      : from_(from), to_(to), recv_(recv) {}

  void begin() {
    roo::thread::attributes attrs;
    attrs.set_name("uart forwarder");
    forwarder_thread_ = roo::thread(attrs, [this]() {
      roo::byte buffer[256];
      while (true) {
        size_t count = from_.read(buffer, sizeof(buffer));
        if (count == 0) {
          break;
        }
        do {
          size_t written = to_.write(buffer, count);
          recv_.notifyDataAvailable();
          if (written == 0) {
            break;
          }
          count -= written;
        } while (count > 0);
      }
    });
  }

 private:
  roo_io::RingPipe& from_;
  roo_io::RingPipe& to_;
  FakeUartDevice& recv_;
  roo::thread forwarder_thread_;
};

struct Emulator {
  FakeUartEndpoint serial1_;
  FakeUartEndpoint serial2_;
  UartForwarder forwarder_1_to_2_;
  UartForwarder forwarder_2_to_1_;
  Emulator()
      : forwarder_1_to_2_(serial1_.tx(), serial2_.rx(), serial2_),
        forwarder_2_to_1_(serial2_.tx(), serial1_.rx(), serial1_) {
    forwarder_1_to_2_.begin();
    forwarder_2_to_1_.begin();

    auto& board = FakeEsp32();
    board.attachUartDevice(serial1_, 27, 14);
    board.attachUartDevice(serial2_, 25, 26);
  }
} emulator;

#endif

// This example demonstrates how to use link messaging for reconnection-capable
// point-to-point message passing.
//
// In this example, we have both sides of the communication channel sending each
// other small packets of data at regular intervals. Reconnections are simply
// ignored.

// Important: to run this example in the loopback mode, you need to connect the
// TX and RX pins of the two UARTs to each other using jumper wires. The pin
// numbers are defined by the constants kPinServerTx, kPinServerRx,
// kPinClientTx, and kPinClientRx below. Make sure to cross the wires, i.e. to
// connect kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.

#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/memory_input_iterator.h"
#include "roo_io/memory/memory_output_iterator.h"
#include "roo_threads.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/reliable_serial.h"
#include "roo_transport/link/link_messaging.h"
#include "roo_transport/rpc/client.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/server.h"

using namespace roo_transport;

#if defined(ESP_PLATFORM)

static const int kPinServerTx = 27;
static const int kPinServerRx = 14;
static const int kPinClientTx = 25;
static const int kPinClientRx = 26;

static const uint32_t kBaudRate = 5000000;

#elif defined(ARDUINO_ARCH_RP2040)

static const int kPinServerTx = 12;
static const int kPinServerRx = 13;
static const int kPinClientTx = 4;
static const int kPinClientRx = 5;

static const uint32_t kBaudRate = 115200;

#else
#error "Unsupported platform"
#endif

// Build for a single microcontroller in loopback mode.
#define MODE_LOOPBACK 0

// Build for the server microcontroller.
#define MODE_SERVER 1

// Build for the client microcontroller.
#define MODE_CLIENT 2

// Select the desired mode.
#define MODE MODE_LOOPBACK
// #define MODE MODE_SERVER
// #define MODE MODE_CLIENT

roo::thread server_thread;

static const int kMaxPayloadSize = 32;

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

ReliableSerial1 server_serial;

// Create the server link messaging layer over the reliable serial transport.
LinkMessaging server_messaging(server_serial, kMaxPayloadSize);

Messaging::SimpleReceiver server_receiver(
    [](Messaging::ConnectionId connection_id, const roo::byte* data,
       size_t len) {
      roo_io::MemoryIterator in(data, data + len);
      roo::string_view msg = roo_io::ReadStringView(in, 24);
      LOG(INFO) << "Server: received message: " << msg;
    });

// This is our server's entry point.
void server() {
#if defined(ESP_PLATFORM)
  Serial1.setRxBufferSize(4096);
  Serial1.begin(kBaudRate, SERIAL_8N1, kPinServerRx, kPinServerTx);
#elif defined(ARDUINO_ARCH_RP2040)
  Serial1.setPinout(kPinServerTx, kPinServerRx);
  Serial1.setFIFOSize(1024);
  Serial1.begin(kBaudRate, SERIAL_8N1);
#endif
  // Initialize the reliable transport layer.
  server_serial.begin();

  // Register the receiver callback for incoming messages. It must be done
  // before calling begin().
  server_messaging.setReceiver(server_receiver);

  // Start the receive loop.
  server_messaging.begin();

  // Send a periodic message every second.
  while (true) {
    roo_io::byte buf[32];
    roo_io::MemoryOutputIterator it(buf, buf + 32);
    roo_io::WriteString(it, "Hello!");
    server_messaging.send(buf, it.ptr() - buf);
    delay(1000);
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

ReliableSerial2 client_serial;

// Create the client link messaging layer over the reliable serial transport.
LinkMessaging client_messaging(client_serial, kMaxPayloadSize);

Messaging::SimpleReceiver client_receiver(
    [](Messaging::ConnectionId connection_id, const roo::byte* data,
       size_t len) {
      roo_io::MemoryIterator in(data, data + len);
      roo::string_view msg = roo_io::ReadStringView(in, 24);
      LOG(INFO) << "Client: received message: " << msg;
    });

void client() {
  // Initialize UART.
#if defined(ESP_PLATFORM)
  Serial2.setRxBufferSize(4096);
  Serial2.begin(kBaudRate, SERIAL_8N1, kPinClientRx, kPinClientTx);
#elif defined(ARDUINO_ARCH_RP2040)
  Serial2.setPinout(kPinClientTx, kPinClientRx);
  Serial2.setFIFOSize(1024);
  Serial2.begin(kBaudRate, SERIAL_8N1);
#endif
  // Initialize the reliable transport layer.
  client_serial.begin();

  // Register the receiver callback for incoming messages. It must be done
  // before calling begin().
  client_messaging.setReceiver(client_receiver);

  // Start the receive loop.
  client_messaging.begin();

  while (true) {
    roo_io::byte buf[32];
    roo_io::MemoryOutputIterator it(buf, buf + 32);
    roo_io::WriteString(it, "Ola!");
    client_messaging.send(buf, it.ptr() - buf);
    delay(1000);
  }
}

#endif

#if (MODE == MODE_CLIENT)

void setup() {
  Serial.begin(115200);
  client();
}

#elif (MODE == MODE_SERVER)

void setup() {
  Serial.begin(115200);
  server();
}

#else  // loopback

// Start the server in a separate task thread, so that it runs concurrently with
// the client (which executes in the main loop).
void startServer() {
  roo::thread::attributes attrs;
  attrs.set_name("server");
  attrs.set_stack_size(4096);
  server_thread = roo::thread(attrs, &server);
}

void setup() {
  Serial.begin(115200);
  startServer();
  client();
}

#endif

// Never called; all the work is done in setup() and new threads.
void loop() {}
