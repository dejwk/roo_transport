#include <Arduino.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_threads.h"
#include "roo_threads/thread.h"
#include "roo_transport/link/link.h"

// Measure a reliable byte stream with a tiny request/response protocol: the
// client requests N bytes, and the server sends N bytes of a known pattern.
// A one-byte response measures round-trip latency; a large response measures
// useful payload throughput, including the cost of receiving and checking it.
//
// Loopback runs both peers on one board. Cross server TX -> client RX and
// client TX -> server RX using the pin constants in your platform section.
// For two boards, choose kServer on one and kClient on the other, cross their
// UART pins, and connect ground. Both peers must use the same baud rate.
//
// Try disconnecting a wire during a transfer: delivery stalls until the wire
// is restored, then the reliable link retransmits and resumes in order.

namespace {
enum class Mode { kLoopback, kServer, kClient };

// Edit these constants directly when experimenting with the example.
static constexpr Mode kMode = Mode::kLoopback;
static constexpr uint32_t kRuns = 0;  // Zero repeats until the board is reset.
static constexpr uint32_t kPayloadBytes = 1024 * 1024;
static constexpr size_t kLatencySamples = 100;
static constexpr size_t kChunkSize = 256;
static constexpr int kIoPriority = 1;
static constexpr bool kWaitForSerial = true;  // Set false for a headless peer.

uint8_t pattern[kChunkSize];
roo::thread server_thread;

// Let producers, consumers and transport senders share the same low priority.
// Receive workers keep their higher priority so incoming UART data is serviced
// promptly. begin(attributes) takes the complete configuration, including
// stack.
roo::thread::attributes SenderAttributes(const char* name) {
  roo::thread::attributes attributes;
  attributes.set_name(name);
  attributes.set_stack_size(4096);
  attributes.set_priority(kIoPriority);
  return attributes;
}
}  // namespace

// Only UART setup and ownership differ by platform. Each section below defines
// its endpoints, initializes the active UARTs, and stops them after use. The
// request/response and measurement code below these sections is shared.
#if defined(ESP32) || defined(ESP_PLATFORM)

#include "roo_transport/link/arduino/reliable_serial.h"

namespace {
static constexpr int kPinServerTx = 4;
static constexpr int kPinServerRx = 2;
static constexpr int kPinClientTx = 17;
static constexpr int kPinClientRx = 16;
static constexpr uint32_t kBaud = 5000000;
static constexpr roo_transport::LinkBufferSize kWindow =
    roo_transport::kBufferSize2KB;

std::unique_ptr<roo_transport::ReliableSerial1> server_uart;
std::unique_ptr<roo_transport::ReliableSerial2> client_uart;

// Software rings must be sized before begin(). The lower RX FIFO threshold
// leaves room for bytes arriving while the interrupt is waiting to run at
// 5 Mbaud; a larger software ring cannot recover a hardware FIFO overflow.
void ConfigureUart(HardwareSerial& uart, int rx_pin, int tx_pin) {
  static constexpr size_t kRxBuffer = 256;
  static constexpr size_t kTxBuffer = 1024;
  CHECK_EQ(uart.setRxBufferSize(kRxBuffer), kRxBuffer);
  CHECK_EQ(uart.setTxBufferSize(kTxBuffer), kTxBuffer);
  uart.begin(kBaud, SERIAL_8N1, rx_pin, tx_pin);
  CHECK(uart.setRxFIFOFull(64));
  CHECK(uart.setRxTimeout(1));
}

// Install UART interrupts on the setup core before starting application tasks.
// ReliableSerial wraps the configured Arduino UART with framing,
// acknowledgments and retransmission; applications use the resulting Link as a
// byte stream.
void InitializeEndpoints() {
  if (kMode != Mode::kClient) {
    ConfigureUart(Serial1, kPinServerRx, kPinServerTx);
    server_uart.reset(new roo_transport::ReliableSerial1(kWindow, kWindow));
    server_uart->begin(SenderAttributes("srv-send"));
  }
  if (kMode != Mode::kServer) {
    ConfigureUart(Serial2, kPinClientRx, kPinClientTx);
    client_uart.reset(new roo_transport::ReliableSerial2(kWindow, kWindow));
    client_uart->begin(SenderAttributes("cli-send"));
  }
}

// Links and their stream adapters must be gone before their transport stops.
void StopEndpoints() {
  if (server_uart != nullptr) {
    server_uart->end();
    server_uart.reset();
    Serial1.end();
  }
  if (client_uart != nullptr) {
    client_uart->end();
    client_uart.reset();
    Serial2.end();
  }
}
}  // namespace

#elif defined(ARDUINO_ARCH_RP2040) && defined(PICO_RP2040) && PICO_RP2040 && \
    defined(__FREERTOS) && !defined(ROO_TESTING)

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>

#include "roo_transport/link/pico/reliable_uart_dma.h"

namespace {
static constexpr int kPinServerTx = 12;
static constexpr int kPinServerRx = 13;
static constexpr int kPinClientTx = 4;
static constexpr int kPinClientRx = 5;
static constexpr uint32_t kBaud = 8312500;
static constexpr roo_transport::LinkBufferSize kWindow =
    roo_transport::kBufferSize8KB;

using Uart = roo_transport::pico::ReliableUartDma;

// Each DMA receiver borrows a ring that must outlive its transport and have
// the required alignment. The driver owns a separate 8 KiB transmit ring.
alignas(Uart::kRxBufferSize) uint8_t server_rx_ring[Uart::kRxBufferSize];
alignas(Uart::kRxBufferSize) uint8_t client_rx_ring[Uart::kRxBufferSize];
std::unique_ptr<Uart> server_uart;
std::unique_ptr<Uart> client_uart;

// Set pins and 8N1 framing on the SDK UART whose data register DMA will use.
void ConfigureUart(uart_inst_t* uart, int rx_pin, int tx_pin) {
  gpio_set_function(tx_pin, GPIO_FUNC_UART);
  gpio_set_function(rx_pin, GPIO_FUNC_UART);
  uart_init(uart, kBaud);
  uart_set_format(uart, 8, 1, UART_PARITY_NONE);
}

// DMA owns these UARTs directly: do not also call Arduino Serial1/2.begin().
// Construct and destroy both endpoints on the setup core so their shared DMA
// interrupt stays on that core. Keep other DMA_IRQ_0 users on that core too.
void InitializeEndpoints() {
  // The default 48 MHz peripheral clock limits UARTs to 3 Mbaud. Using clk_sys
  // permits the baud rate above without overclocking the CPU. Configure other
  // peripherals after making this application-wide clock choice.
  const uint32_t system_hz = clock_get_hz(clk_sys);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  system_hz, system_hz);

  // A DMA constructor immediately arms reception. Configure every active UART
  // first, so a receiver does not see its peer's pins being switched to UART.
  if (kMode != Mode::kClient) {
    ConfigureUart(uart0, kPinServerRx, kPinServerTx);
  }
  if (kMode != Mode::kServer) {
    ConfigureUart(uart1, kPinClientRx, kPinClientTx);
  }
  if (kMode != Mode::kClient) {
    server_uart.reset(new Uart(uart0, server_rx_ring, kWindow, kWindow));
    server_uart->begin(SenderAttributes("srv-send"));
  }
  if (kMode != Mode::kServer) {
    client_uart.reset(new Uart(uart1, client_rx_ring, kWindow, kWindow));
    client_uart->begin(SenderAttributes("cli-send"));
  }
}

// Run on the same setup core, after application I/O and Links have finished.
void StopEndpoints() {
  if (server_uart != nullptr) {
    server_uart->end();
    server_uart.reset();
  }
  if (client_uart != nullptr) {
    client_uart->end();
    client_uart.reset();
  }
}
}  // namespace

#elif defined(ARDUINO_ARCH_RP2040)

#if !defined(__FREERTOS) && !defined(ROO_TESTING)
#error "Enable Arduino-Pico FreeRTOS for this threaded benchmark"
#endif

#include "roo_transport/link/arduino/reliable_serial.h"

// Other Arduino-Pico builds use the Arduino UART adapter. This slower setup
// also explains the ordinary SerialUART path without RP2040 DMA ownership.
namespace {
static constexpr int kPinServerTx = 12;
static constexpr int kPinServerRx = 13;
static constexpr int kPinClientTx = 4;
static constexpr int kPinClientRx = 5;
static constexpr uint32_t kBaud = 115200;
static constexpr roo_transport::LinkBufferSize kWindow =
    roo_transport::kBufferSize4KB;

std::unique_ptr<roo_transport::ReliableSerial1> server_uart;
std::unique_ptr<roo_transport::ReliableSerial2> client_uart;

void InitializeEndpoints() {
  if (kMode != Mode::kClient) {
    Serial1.setPinout(kPinServerTx, kPinServerRx);
    Serial1.setFIFOSize(1024);
    Serial1.begin(kBaud, SERIAL_8N1);
    server_uart.reset(new roo_transport::ReliableSerial1(kWindow, kWindow));
    server_uart->begin();
  }
  if (kMode != Mode::kServer) {
    Serial2.setPinout(kPinClientTx, kPinClientRx);
    Serial2.setFIFOSize(1024);
    Serial2.begin(kBaud, SERIAL_8N1);
    client_uart.reset(new roo_transport::ReliableSerial2(kWindow, kWindow));
    client_uart->begin();
  }
}

void StopEndpoints() {
  // The Arduino adapter cannot stop an idle receive worker. Keep its global
  // endpoints alive until reset; the native DMA path above supports shutdown.
}
}  // namespace

#else
#error "Unsupported platform"
#endif

#if defined(ROO_TESTING) && (defined(ESP32) || defined(ESP_PLATFORM))
// Host-only wiring for the same example. On a board, use the crossed jumpers
// described above; none of this fixture is needed in the application itself.
#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"

namespace {
/// Replaces physical jumper wires with the host fake UART cable.
struct Emulator {
  /// Connects the same pin choices used by the ESP32 setup above.
  Emulator() {
    FakeEsp32().attachUartDevice(cable.end_a(), kPinServerTx, kPinServerRx);
    FakeEsp32().attachUartDevice(cable.end_b(), kPinClientTx, kPinClientRx);
  }

  FakeUartCable cable;
} emulator;
}  // namespace
#endif

namespace {
// The server reads a length, sends that many pattern bytes, then waits for the
// next length. A varint encodes a request for one response byte in one byte.
void RunServer() {
  roo_transport::Link link = server_uart->transport().connectOrDie();
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  while (true) {
    uint64_t remaining = in.readVarU64();
    if (in.status() == roo_io::kEndOfStream) break;
    CHECK_EQ(in.status(), roo_io::kOk);
    while (remaining != 0) {
      const size_t count = std::min<uint64_t>(remaining, kChunkSize);
      out.writeByteArray(reinterpret_cast<const roo::byte*>(pattern), count);
      remaining -= count;
    }
    // Release the final partial packet without waiting for more application
    // data. flush() does not mean the peer has already received the response.
    out.flush();
    CHECK_EQ(out.status(), roo_io::kOk);
  }
  // Request EOF ends the session. Close the response too, so a finite client
  // can observe that all server output has finished.
  out.close();
}

// Time a complete one-byte request/response, not just the local UART write.
// The median (p50) describes a typical round trip; p99 shows the slower tail.
void LatencyTest(roo_io::InputStreamReader& in,
                 roo_io::OutputStreamWriter& out) {
  std::unique_ptr<float[]> rtt(new float[kLatencySamples]);
  for (size_t sample = 0; sample < kLatencySamples; ++sample) {
    const roo_time::Uptime start = roo_time::Uptime::Now();
    out.writeVarU64(1);
    out.flush();  // Send this small request now, rather than filling a packet.
    CHECK_EQ(in.readU8(), pattern[0]);
    CHECK_EQ(in.status(), roo_io::kOk);
    rtt[sample] = (roo_time::Uptime::Now() - start).inMillisFloat();
  }
  std::sort(rtt.get(), rtt.get() + kLatencySamples);
  Serial.printf(
      "Round-trip times [ms] (min, p50, p90, p99, max): %f, %f, %f, %f, %f\n",
      rtt[0], rtt[kLatencySamples / 2], rtt[kLatencySamples * 9 / 10],
      rtt[kLatencySamples * 99 / 100], rtt[kLatencySamples - 1]);
}

// Request one large response and read it in small reusable blocks. Count only
// useful payload bits: 8N1 already uses ten wire bits per byte, and framing,
// ACKs and retries use more. Byte validation is included in the timed interval.
void ThroughputTest(roo_io::InputStreamReader& in,
                    roo_io::OutputStreamWriter& out, uint32_t payload_bytes) {
  // Use the heap so increasing the block size does not exhaust Pico's small
  // setup()/loop() stack. There is no allocation per received block.
  std::unique_ptr<uint8_t[]> received(new uint8_t[kChunkSize]);
  uint32_t remaining = payload_bytes;
  const roo_time::Uptime start = roo_time::Uptime::Now();
  out.writeVarU64(payload_bytes);
  out.flush();
  while (remaining != 0) {
    const size_t wanted = std::min<uint32_t>(remaining, kChunkSize);
    const size_t count =
        in.readByteArray(reinterpret_cast<roo::byte*>(received.get()), wanted);
    CHECK_EQ(count, wanted);
    CHECK_EQ(memcmp(received.get(), pattern, count), 0);
    remaining -= count;
  }
  const uint64_t elapsed = (roo_time::Uptime::Now() - start).inMicros();
  CHECK_EQ(in.status(), roo_io::kOk);
  CHECK_EQ(out.status(), roo_io::kOk);
  Serial.printf("Throughput for %lu bytes: %.6f Mbit/s\n",
                static_cast<unsigned long>(payload_bytes),
                payload_bytes * 8.0 / elapsed);
}

// Keep readers/writers inside their Link's lifetime. A finite client closes
// its requests and waits for response EOF before allowing transport shutdown.
void RunClient(uint32_t runs, uint32_t payload_bytes) {
  roo_transport::Link link = client_uart->transport().connectOrDie();
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  for (uint32_t run = 0; runs == 0 || run < runs; ++run) {
    LatencyTest(in, out);
    ThroughputTest(in, out, payload_bytes);
  }
  out.close();
  uint8_t final_byte;
  CHECK_EQ(in.readByteArray(reinterpret_cast<roo::byte*>(&final_byte), 1), 0u);
  CHECK_EQ(in.status(), roo_io::kEndOfStream);
}

// The client blocks while waiting for responses, so loopback needs a concurrent
// server. Initialize and stop hardware on this calling task (Arduino setup).
void RunBenchmark(uint32_t runs, uint32_t payload_bytes) {
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<uint8_t>(i);
  }
  vTaskPrioritySet(nullptr, kIoPriority);
  InitializeEndpoints();
  if (kMode == Mode::kLoopback) {
    roo::thread::attributes attributes;
    attributes.set_name("server");
    attributes.set_stack_size(8192);
    attributes.set_priority(kIoPriority);
    server_thread = roo::thread(attributes, RunServer);
    RunClient(runs, payload_bytes);
    server_thread.join();
  } else if (kMode == Mode::kServer) {
    RunServer();
  } else {
    RunClient(runs, payload_bytes);
  }
  StopEndpoints();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  while (kWaitForSerial && !static_cast<bool>(Serial)) delay(10);
  RunBenchmark(kRuns, kPayloadBytes);
}

// Continuous runs stay in RunClient(). Finite runs reach the idle Arduino loop.
void loop() { delay(1000); }
