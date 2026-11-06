// Send small structured messages reliably between two UARTs on one board.
// Cross kPinServerTx to kPinClientRx, and kPinClientTx to kPinServerRx with
// jumper wires. The server writes messages; the client reads the same fields
// in the same order. Both run concurrently because stream reads can block.
//
// Edit the constants below to change the wiring or pace. This example favors
// clarity over bulk throughput; see ../Benchmark/README.md for the bulk
// example.

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "roo_io.h"
#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_threads.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/reliable_serial.h"

using namespace roo_transport;

static constexpr uint32_t kMessageIntervalMs = 2;
static constexpr uint32_t kPrintEveryMessages = 1000;
static constexpr int kServerPriority = 1;

// The UART setup is the only platform-dependent part of the message exchange.
// Initialize both UARTs before starting either application thread.
#if defined(ESP32) || defined(ESP_PLATFORM)

static constexpr int kPinServerTx = 27;
static constexpr int kPinServerRx = 14;
static constexpr int kPinClientTx = 25;
static constexpr int kPinClientRx = 26;
static constexpr uint32_t kBaudRate = 5000000;
static constexpr size_t kRxBufferSize = 4096;

void ConfigureUarts() {
  Serial1.setRxBufferSize(kRxBufferSize);
  Serial1.begin(kBaudRate, SERIAL_8N1, kPinServerRx, kPinServerTx);
  Serial2.setRxBufferSize(kRxBufferSize);
  Serial2.begin(kBaudRate, SERIAL_8N1, kPinClientRx, kPinClientTx);
}

#elif defined(ARDUINO_ARCH_RP2040)

static constexpr int kPinServerTx = 12;
static constexpr int kPinServerRx = 13;
static constexpr int kPinClientTx = 4;
static constexpr int kPinClientRx = 5;
static constexpr uint32_t kBaudRate = 115200;
static constexpr size_t kRxBufferSize = 1024;

void ConfigureUarts() {
  Serial1.setPinout(kPinServerTx, kPinServerRx);
  Serial1.setFIFOSize(kRxBufferSize);
  Serial1.begin(kBaudRate, SERIAL_8N1);
  Serial2.setPinout(kPinClientTx, kPinClientRx);
  Serial2.setFIFOSize(kRxBufferSize);
  Serial2.begin(kBaudRate, SERIAL_8N1);
}

#else
#error "Unsupported platform"
#endif

#ifdef ROO_TESTING
// Host emulation replaces the jumper wires with a byte cable. Skip this
// section when reading the example for a physical board.
#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"

/// Supplies the physical loopback connection in host emulation.
struct Emulator {
  /// Connects the emulated UARTs using the pin choices above.
  Emulator() {
    FakeEsp32().attachUartDevice(cable.end_a(), kPinServerTx, kPinServerRx);
    FakeEsp32().attachUartDevice(cable.end_b(), kPinClientTx, kPinClientRx);
  }

  FakeUartCable cable;
} emulator;
#endif

roo::thread server_thread;

// These wrappers add framing, acknowledgments and retransmission to Serial1
// and Serial2. Applications exchange an ordered byte stream through the Link.
ReliableSerial1 reliable_serial1;
ReliableSerial2 reliable_serial2;

// Sends a sequence number followed by a string for each message.
void RunServer() {
  LOG(INFO) << "Server connecting...";
  LinkStream link = reliable_serial1.connectOrDie();
  LOG(INFO) << "Server connected.";

  roo_io::OutputStreamWriter out(link.out());
  uint32_t sequence = 0;
  while (true) {
    // Specify the integer's byte order explicitly. Strings carry their length,
    // so the receiver knows where the next message starts without a delimiter.
    out.writeBeU32(sequence++);
    out.writeString("Hello, world!\n");
    // Release this small message for transmission without waiting to fill a
    // transport packet.
    out.flush();
    // Pace greetings and give other tasks time to run. This delay is deliberate
    // for a readable message demo; a bulk sender would keep writing instead.
    delay(kMessageIntervalMs);
  }
}

// The server must run separately: the client blocks while waiting for bytes.
void StartServer() {
  roo::thread::attributes attrs;
  attrs.set_name("server");
  // Keep application work at a modest priority so the receiver can progress.
  attrs.set_priority(kServerPriority);
  server_thread = roo::thread(attrs, RunServer);
}

// Decodes exactly the fields written by RunServer(), in the same order.
void RunClient() {
  LOG(INFO) << "Client connecting...";
  LinkStream link = reliable_serial2.connectOrDie();
  CHECK_EQ(link.status(), LinkStatus::kConnected);
  LOG(INFO) << "Client connected.";

  roo_io::InputStreamReader in(link.in());
  while (true) {
    // Reads wait for complete values even when UART packets arrive in pieces.
    const uint32_t sequence = in.readBeU32();
    const std::string message = in.readString();
    if (sequence % kPrintEveryMessages == 0) {
      // Print only occasional messages so console output does not dominate.
      LOG(INFO) << "Received " << sequence << ": " << message;
    }
  }
}

void setup() {
  Serial.begin(115200);
  ConfigureUarts();
  // Start both transports on the UART initialization core. Application threads
  // can then connect and exchange data without configuring hardware themselves.
  reliable_serial1.begin();
  reliable_serial2.begin();
  StartServer();
  RunClient();
}

// RunClient() continues in setup(), so Arduino never reaches loop().
void loop() {}
