#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040) && !defined(__FREERTOS) && \
    !defined(ROO_TESTING)
#error "Enable Arduino-Pico FreeRTOS for this threaded benchmark"
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_threads.h"
#include "roo_threads/thread.h"
#include "roo_transport/link/arduino/reliable_serial.h"
#include "roo_transport/link/link.h"

// Cross server TX with client RX, and server RX with client TX. MODE selects a
// one-board loopback or one endpoint of a two-board connection. README.md lists
// the measured configuration, overridable build flags and memory/scheduling
// cost.
#define MODE_LOOPBACK 0
#define MODE_SERVER 1
#define MODE_CLIENT 2
#ifndef MODE
#define MODE MODE_LOOPBACK
#endif
#if MODE != MODE_LOOPBACK && MODE != MODE_SERVER && MODE != MODE_CLIENT
#error "MODE must be MODE_LOOPBACK, MODE_SERVER or MODE_CLIENT"
#endif

#if defined(ARDUINO_ARCH_RP2040) && defined(PICO_RP2040) && PICO_RP2040 && \
    defined(__FREERTOS) && !defined(ROO_TESTING)
#define ROO_TRANSPORT_BENCHMARK_PICO_DMA 1
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>

#include "roo_transport/link/pico/reliable_uart_dma.h"
#else
#define ROO_TRANSPORT_BENCHMARK_PICO_DMA 0
#endif

#if defined(ESP32) || defined(ESP_PLATFORM)
constexpr int kPinServerTx = 4;
constexpr int kPinServerRx = 2;
constexpr int kPinClientTx = 17;
constexpr int kPinClientRx = 16;
#ifndef ROO_TRANSPORT_BENCHMARK_BAUD
#define ROO_TRANSPORT_BENCHMARK_BAUD 5000000
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_RX_BUFFER
#define ROO_TRANSPORT_BENCHMARK_RX_BUFFER 256
#endif
#elif defined(ARDUINO_ARCH_RP2040)
constexpr int kPinServerTx = 12;
constexpr int kPinServerRx = 13;
constexpr int kPinClientTx = 4;
constexpr int kPinClientRx = 5;
#ifndef ROO_TRANSPORT_BENCHMARK_BAUD
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
#define ROO_TRANSPORT_BENCHMARK_BAUD 8312500
#else
#define ROO_TRANSPORT_BENCHMARK_BAUD 115200
#endif
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_RX_BUFFER
#define ROO_TRANSPORT_BENCHMARK_RX_BUFFER 1024
#endif
#else
#error "Unsupported platform"
#endif

#ifndef ROO_TRANSPORT_BENCHMARK_TX_BUFFER
#define ROO_TRANSPORT_BENCHMARK_TX_BUFFER 1024
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_RX_FIFO
#define ROO_TRANSPORT_BENCHMARK_RX_FIFO 64
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_RX_TIMEOUT
#define ROO_TRANSPORT_BENCHMARK_RX_TIMEOUT 1
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
#define ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2 5
#elif defined(ESP32) || defined(ESP_PLATFORM)
#define ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2 3
#else
#define ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2 4
#endif
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_IO_PRIORITY
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA || defined(ESP32) || defined(ESP_PLATFORM)
#define ROO_TRANSPORT_BENCHMARK_IO_PRIORITY 1
#else
#define ROO_TRANSPORT_BENCHMARK_IO_PRIORITY 0
#endif
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_SENDER_PRIORITY
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA || defined(ESP32) || defined(ESP_PLATFORM)
#define ROO_TRANSPORT_BENCHMARK_SENDER_PRIORITY 1
#else
#define ROO_TRANSPORT_BENCHMARK_SENDER_PRIORITY (configMAX_PRIORITIES - 2)
#endif
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_CHUNK_BYTES
#define ROO_TRANSPORT_BENCHMARK_CHUNK_BYTES 256
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_LATENCY_SAMPLES
#define ROO_TRANSPORT_BENCHMARK_LATENCY_SAMPLES 100
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_PAYLOAD_BYTES
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA || defined(ESP32) || defined(ESP_PLATFORM)
#define ROO_TRANSPORT_BENCHMARK_PAYLOAD_BYTES 1048576
#else
#define ROO_TRANSPORT_BENCHMARK_PAYLOAD_BYTES (ROO_TRANSPORT_BENCHMARK_BAUD / 5)
#endif
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_RUNS
#define ROO_TRANSPORT_BENCHMARK_RUNS 0
#endif
#ifndef ROO_TRANSPORT_BENCHMARK_WAIT_FOR_SERIAL
#define ROO_TRANSPORT_BENCHMARK_WAIT_FOR_SERIAL ROO_TRANSPORT_BENCHMARK_PICO_DMA
#endif

// The native Pico branch owns DMA hardware. Emulation uses its Arduino
// transport instead; only the ESP32 fake board fixture belongs in this
// capability branch.
#if defined(ROO_TESTING) && (defined(ESP32) || defined(ESP_PLATFORM))
#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"

namespace {
/// Connects both UARTs in ESP32 emulation before any driver initialization.
struct Emulator {
  /// Attaches the crossed byte cable to the example's ESP32 pin choices.
  Emulator() {
    FakeEsp32().attachUartDevice(cable.end_a(), kPinServerTx, kPinServerRx);
    FakeEsp32().attachUartDevice(cable.end_b(), kPinClientTx, kPinClientRx);
  }

  FakeUartCable cable;
} emulator;
}  // namespace
#endif

namespace {
constexpr uint32_t kBaud = ROO_TRANSPORT_BENCHMARK_BAUD;
constexpr uint32_t kPayloadBytes = ROO_TRANSPORT_BENCHMARK_PAYLOAD_BYTES;
constexpr size_t kLatencySamples = ROO_TRANSPORT_BENCHMARK_LATENCY_SAMPLES;
constexpr uint32_t kRuns = ROO_TRANSPORT_BENCHMARK_RUNS;
constexpr int kIoPriority = ROO_TRANSPORT_BENCHMARK_IO_PRIORITY;
constexpr int kSenderPriority = ROO_TRANSPORT_BENCHMARK_SENDER_PRIORITY;
constexpr auto kWindow = static_cast<roo_transport::LinkBufferSize>(
    ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2);
constexpr size_t kChunkSize = ROO_TRANSPORT_BENCHMARK_CHUNK_BYTES;
static_assert(kChunkSize >= 256 && kChunkSize <= 4096 && kChunkSize % 256 == 0,
              "Use 256-byte multiples up to 4096 for periodic test data");
static_assert(kBaud > 0 && kPayloadBytes > 0,
              "Use positive baud/payload sizes");
static_assert(kLatencySamples > 0, "At least one latency sample is required");
static_assert(ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2 >= 0 &&
                  ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2 <= 10,
              "Window log2 must be 0..10 (256 bytes through 256 KiB)");
static_assert(kIoPriority >= 0 && kIoPriority < configMAX_PRIORITIES - 1,
              "Leave a higher priority available for UART receive workers");
static_assert(kSenderPriority >= 0 &&
                  kSenderPriority < configMAX_PRIORITIES - 1,
              "Leave a higher priority available for UART receive workers");

uint8_t pattern[kChunkSize];
roo::thread server_thread;

#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
using ServerUart = roo_transport::pico::ReliableUartDma;
using ClientUart = roo_transport::pico::ReliableUartDma;
#if MODE != MODE_CLIENT
alignas(ServerUart::kRxBufferSize) uint8_t
    server_rx_ring[ServerUart::kRxBufferSize];
#endif
#if MODE != MODE_SERVER
alignas(ClientUart::kRxBufferSize) uint8_t
    client_rx_ring[ClientUart::kRxBufferSize];
#endif
#else
using ServerUart = roo_transport::ReliableSerial1;
using ClientUart = roo_transport::ReliableSerial2;
#endif

#if MODE != MODE_CLIENT
std::unique_ptr<ServerUart> server_uart;
#endif
#if MODE != MODE_SERVER
std::unique_ptr<ClientUart> client_uart;
#endif

// Both endpoints are configured serially by setup(), before application workers
// start. The IDF UART IRQ or Pico DMA completion IRQ stays on this setup core.
void InitializeEndpoints() {
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
  const uint32_t system_hz = clock_get_hz(clk_sys);
  // A 48 MHz peripheral clock caps this UART at 3 Mbaud. This dedicated example
  // explicitly selects clk_sys before configuring either UART, without changing
  // the CPU clock. Other application peripherals must be configured afterward.
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  system_hz, system_hz);
#endif
#if MODE != MODE_CLIENT
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
  gpio_set_function(kPinServerTx, GPIO_FUNC_UART);
  gpio_set_function(kPinServerRx, GPIO_FUNC_UART);
  const uint32_t server_baud = uart_init(uart0, kBaud);
  uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
  Serial.printf("UART server actual_baud=%lu\n",
                static_cast<unsigned long>(server_baud));
#elif defined(ESP32) || defined(ESP_PLATFORM)
  CHECK_EQ(Serial1.setRxBufferSize(ROO_TRANSPORT_BENCHMARK_RX_BUFFER),
           ROO_TRANSPORT_BENCHMARK_RX_BUFFER);
  CHECK_EQ(Serial1.setTxBufferSize(ROO_TRANSPORT_BENCHMARK_TX_BUFFER),
           ROO_TRANSPORT_BENCHMARK_TX_BUFFER);
  Serial1.begin(kBaud, SERIAL_8N1, kPinServerRx, kPinServerTx);
  CHECK(Serial1.setRxFIFOFull(ROO_TRANSPORT_BENCHMARK_RX_FIFO));
  CHECK(Serial1.setRxTimeout(ROO_TRANSPORT_BENCHMARK_RX_TIMEOUT));
#else
  Serial1.setPinout(kPinServerTx, kPinServerRx);
  Serial1.setFIFOSize(ROO_TRANSPORT_BENCHMARK_RX_BUFFER);
  Serial1.begin(kBaud, SERIAL_8N1);
#endif
#endif
#if MODE != MODE_SERVER
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
  gpio_set_function(kPinClientTx, GPIO_FUNC_UART);
  gpio_set_function(kPinClientRx, GPIO_FUNC_UART);
  const uint32_t client_baud = uart_init(uart1, kBaud);
  uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
  Serial.printf("UART client actual_baud=%lu\n",
                static_cast<unsigned long>(client_baud));
#elif defined(ESP32) || defined(ESP_PLATFORM)
  CHECK_EQ(Serial2.setRxBufferSize(ROO_TRANSPORT_BENCHMARK_RX_BUFFER),
           ROO_TRANSPORT_BENCHMARK_RX_BUFFER);
  CHECK_EQ(Serial2.setTxBufferSize(ROO_TRANSPORT_BENCHMARK_TX_BUFFER),
           ROO_TRANSPORT_BENCHMARK_TX_BUFFER);
  Serial2.begin(kBaud, SERIAL_8N1, kPinClientRx, kPinClientTx);
  CHECK(Serial2.setRxFIFOFull(ROO_TRANSPORT_BENCHMARK_RX_FIFO));
  CHECK(Serial2.setRxTimeout(ROO_TRANSPORT_BENCHMARK_RX_TIMEOUT));
#else
  Serial2.setPinout(kPinClientTx, kPinClientRx);
  Serial2.setFIFOSize(ROO_TRANSPORT_BENCHMARK_RX_BUFFER);
  Serial2.begin(kBaud, SERIAL_8N1);
#endif
#endif
  // Configure every UART first: the Pico constructor immediately arms RX DMA
  // and should see an initialized peer, not a pin being switched to UART mode.
#if MODE != MODE_CLIENT
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
  server_uart.reset(new ServerUart(uart0, server_rx_ring, kWindow, kWindow));
#else
  server_uart.reset(new ServerUart(kWindow, kWindow));
#endif
#endif
#if MODE != MODE_SERVER
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
  client_uart.reset(new ClientUart(uart1, client_rx_ring, kWindow, kWindow));
#else
  client_uart.reset(new ClientUart(kWindow, kWindow));
#endif
#endif
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA || defined(ESP32) || defined(ESP_PLATFORM)
  // These attributes configure only the sender worker. The UART receive worker
  // or Arduino event task keeps its existing higher-priority configuration.
  roo::thread::attributes sender_attributes;
  sender_attributes.set_stack_size(4096);
  sender_attributes.set_priority(kSenderPriority);
#if MODE != MODE_CLIENT
  sender_attributes.set_name("srv-send");
  server_uart->begin(sender_attributes);
#endif
#if MODE != MODE_SERVER
  sender_attributes.set_name("cli-send");
  client_uart->begin(sender_attributes);
#endif
#else
#if MODE != MODE_CLIENT
  server_uart->begin();
#endif
#if MODE != MODE_SERVER
  client_uart->begin();
#endif
#endif
}

// EOF makes a finite client work with a server using any latency-sample count.
#if MODE != MODE_CLIENT
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
    out.flush();
    CHECK_EQ(out.status(), roo_io::kOk);
  }
  out.close();
}
#endif

#if MODE != MODE_SERVER
// Small requests measure latency independently of the bulk-write configuration.
void LatencyTest(roo_io::InputStreamReader& in,
                 roo_io::OutputStreamWriter& out) {
  std::unique_ptr<float[]> rtt(new float[kLatencySamples]);
  for (size_t sample = 0; sample < kLatencySamples; ++sample) {
    const roo_time::Uptime start = roo_time::Uptime::Now();
    out.writeVarU64(1);
    out.flush();
    CHECK_EQ(in.readU8(), pattern[0]);
    rtt[sample] = (roo_time::Uptime::Now() - start).inMillisFloat();
  }
  std::sort(rtt.get(), rtt.get() + kLatencySamples);
  Serial.printf("LATENCY_MS min=%f p50=%f p90=%f p99=%f max=%f\n", rtt[0],
                rtt[kLatencySamples / 2], rtt[kLatencySamples * 9 / 10],
                rtt[kLatencySamples * 99 / 100], rtt[kLatencySamples - 1]);
}

// Count only delivered and pattern-validated payload. Logging and ACK settling
// are outside the measured interval; packet counter differences are
// diagnostics.
void ThroughputTest(roo_io::InputStreamReader& in,
                    roo_io::OutputStreamWriter& out) {
#if MODE == MODE_LOOPBACK
  roo_transport::LinkTransport::StatsMonitor stats(server_uart->transport());
#else
  roo_transport::LinkTransport::StatsMonitor stats(client_uart->transport());
#endif
  delay(25);
  const uint32_t sent_before = stats.packets_sent();
  const uint32_t delivered_before = stats.packets_delivered();
  const uint32_t received_before = stats.packets_received();
#if defined(ARDUINO_ARCH_RP2040)
  // Arduino-Pico gives setup()/loop() a 4 KiB stack. Keep bulk buffers off it.
  std::unique_ptr<uint8_t[]> received_storage(new uint8_t[kChunkSize]);
  uint8_t* received = received_storage.get();
#else
  uint8_t received[kChunkSize];
#endif
  uint32_t remaining = kPayloadBytes;
  uint32_t bad = 0;
  const roo_time::Uptime start = roo_time::Uptime::Now();
  out.writeVarU64(kPayloadBytes);
  out.flush();
  while (remaining != 0) {
    const size_t wanted = std::min<uint32_t>(remaining, kChunkSize);
    const size_t count =
        in.readByteArray(reinterpret_cast<roo::byte*>(received), wanted);
    CHECK_EQ(count, wanted);
    // Validate the whole block efficiently; count individual mismatches only
    // on a failing transfer. The reader fills each requested producer block.
    if (memcmp(received, pattern, count) != 0) {
      for (size_t i = 0; i < count; ++i) {
        if (received[i] != pattern[i]) ++bad;
      }
    }
    remaining -= count;
  }
  const uint64_t elapsed = (roo_time::Uptime::Now() - start).inMicros();
  CHECK_EQ(bad, 0u);
  CHECK_EQ(in.status(), roo_io::kOk);
  CHECK_EQ(out.status(), roo_io::kOk);
  delay(25);
  Serial.printf("RESULT bytes=%lu us=%llu mbps=%.6f bad=%lu\n",
                static_cast<unsigned long>(kPayloadBytes),
                static_cast<unsigned long long>(elapsed),
                kPayloadBytes * 8.0 / elapsed, static_cast<unsigned long>(bad));
  Serial.printf(
      "PACKETS local=%s sent=%lu delivered=%lu received=%lu\n",
#if MODE == MODE_LOOPBACK
      "server",
#else
      "client",
#endif
      static_cast<unsigned long>(stats.packets_sent() - sent_before),
      static_cast<unsigned long>(stats.packets_delivered() - delivered_before),
      static_cast<unsigned long>(stats.packets_received() - received_before));
}

void RunClient() {
  roo_transport::Link link = client_uart->transport().connectOrDie();
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  for (uint32_t run = 0; kRuns == 0 || run < kRuns; ++run) {
    LatencyTest(in, out);
    ThroughputTest(in, out);
  }
  out.close();
  uint8_t final_byte;
  CHECK_EQ(in.readByteArray(reinterpret_cast<roo::byte*>(&final_byte), 1), 0u);
  CHECK_EQ(in.status(), roo_io::kEndOfStream);
}
#endif

// Native DMA/ESP32 workers stop after Links and their stream adapters are gone.
// The legacy Pico Arduino adapter has no idle-stop support; keep that global
// adapter alive in a finite emulator workload rather than adding a wakeup hack.
void StopEndpoints() {
#if defined(ARDUINO_ARCH_RP2040) && !ROO_TRANSPORT_BENCHMARK_PICO_DMA
  Serial.println("NOTE legacy Pico receive workers remain alive until reset");
#else
#if MODE != MODE_CLIENT
  server_uart->end();
  server_uart.reset();
#if defined(ESP32) || defined(ESP_PLATFORM)
  Serial1.end();
#endif
#endif
#if MODE != MODE_SERVER
  client_uart->end();
  client_uart.reset();
#if defined(ESP32) || defined(ESP_PLATFORM)
  Serial2.end();
#endif
#endif
#endif
}

void PrintConfiguration() {
  Serial.printf(
      "CONFIG mode=%d baud=%lu window_log2=%d io_priority=%d payload=%lu "
      "runs=%lu chunk=%u\n",
      MODE, static_cast<unsigned long>(kBaud),
      ROO_TRANSPORT_BENCHMARK_WINDOW_LOG2, kIoPriority,
      static_cast<unsigned long>(kPayloadBytes),
      static_cast<unsigned long>(kRuns), static_cast<unsigned>(kChunkSize));
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA || defined(ESP32) || defined(ESP_PLATFORM)
  Serial.printf("SENDER priority=%d stack=4096\n", kSenderPriority);
#endif
#if ROO_TRANSPORT_BENCHMARK_PICO_DMA
  Serial.printf("BACKEND pico-dma cpu=%lu peri=%lu rx_ring=%u tx_ring=%u\n",
                static_cast<unsigned long>(clock_get_hz(clk_sys)),
                static_cast<unsigned long>(clock_get_hz(clk_peri)),
                static_cast<unsigned>(ServerUart::kRxBufferSize), 8192u);
#elif defined(ESP32) || defined(ESP_PLATFORM)
  Serial.printf(
      "BACKEND esp32-idf rx_buffer=%u tx_buffer=%u rx_fifo=%u rx_timeout=%u\n",
      ROO_TRANSPORT_BENCHMARK_RX_BUFFER, ROO_TRANSPORT_BENCHMARK_TX_BUFFER,
      ROO_TRANSPORT_BENCHMARK_RX_FIFO, ROO_TRANSPORT_BENCHMARK_RX_TIMEOUT);
#else
  Serial.printf("BACKEND pico-arduino fifo=%u\n",
                ROO_TRANSPORT_BENCHMARK_RX_BUFFER);
#endif
}
}  // namespace

void setup() {
  Serial.begin(115200);
#if ROO_TRANSPORT_BENCHMARK_WAIT_FOR_SERIAL
  while (!static_cast<bool>(Serial)) delay(10);
#else
  delay(250);
#endif
  for (size_t i = 0; i < sizeof(pattern); ++i) pattern[i] = i;
  if (kIoPriority != 0) vTaskPrioritySet(nullptr, kIoPriority);
  InitializeEndpoints();
  PrintConfiguration();
#if MODE == MODE_LOOPBACK
  roo::thread::attributes attrs;
  attrs.set_name("server");
  attrs.set_stack_size(8192);
  attrs.set_priority(kIoPriority == 0 ? 1 : kIoPriority);
  server_thread = roo::thread(attrs, RunServer);
  RunClient();
  server_thread.join();
#elif MODE == MODE_SERVER
  RunServer();
#else
  RunClient();
#endif
  StopEndpoints();
  Serial.println("DONE");
}

void loop() { delay(1000); }
