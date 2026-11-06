#include "roo_transport/link/internal/in_buffer.h"
#include "roo_transport/link/internal/out_buffer.h"
#include <Arduino.h>
#include <driver/uart.h>
#include <esp_heap_caps.h>
#include <freertos/ringbuf.h>

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
static constexpr Mode kMode = Mode::kClient;
static constexpr uint32_t kRuns = 1; // Zero repeats until the board is reset.
static constexpr uint32_t kPayloadBytes = 1048576;
static constexpr size_t kLatencySamples = 100;
static constexpr size_t kChunkSize = 256;
static constexpr int kIoPriority = 1;
static constexpr bool kWaitForSerial = false; // Set false for a headless peer.

uint8_t pattern[kChunkSize];
roo::thread server_thread;

// Heap probes run between benchmark phases, never inside timed transfers.
void RamSnapshot(const char *stage) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  char line[256];
  const int count =
      snprintf(line, sizeof(line),
               "RAM role=client stage=%s free=%u allocated=%u min_free=%u "
               "largest=%u blocks=%u\n",
               stage, static_cast<unsigned>(info.total_free_bytes),
               static_cast<unsigned>(info.total_allocated_bytes),
               static_cast<unsigned>(info.minimum_free_bytes),
               static_cast<unsigned>(info.largest_free_block),
               static_cast<unsigned>(info.allocated_blocks));
  Serial.write(reinterpret_cast<const uint8_t *>(line), count);
}

void RamStackWatermarks() {
  TaskHandle_t sender = xTaskGetHandle("cli-send");
  TaskHandle_t receiver = xTaskGetHandle("uart_event_task");
  CHECK(sender != nullptr);
  CHECK(receiver != nullptr);
  char line[192];
  const int count =
      snprintf(line, sizeof(line),
               "RAMSTACK role=client sender_free=%u receiver_free=%u "
               "app_free=%u stack_type=%u\n",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(sender)),
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(receiver)),
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
               static_cast<unsigned>(sizeof(StackType_t)));
  Serial.write(reinterpret_cast<const uint8_t *>(line), count);
}

// Let producers, consumers and transport senders share the same low priority.
// Receive workers keep their higher priority so incoming UART data is serviced
// promptly. begin(attributes) takes the complete configuration, including
// stack.
roo::thread::attributes SenderAttributes(const char *name) {
  roo::thread::attributes attributes;
  attributes.set_name(name);
  attributes.set_stack_size(4096);
  attributes.set_priority(kIoPriority);
  return attributes;
}
} // namespace

// Only UART setup and ownership differ by platform. Each section below defines
// its endpoints, initializes the active UARTs, and stops them after use. The
// request/response and measurement code below these sections is shared.
#if defined(ESP32) || defined(ESP_PLATFORM)

#include "roo_transport/link/arduino/reliable_serial.h"

namespace {
static constexpr int kPinServerTx = 4;
static constexpr int kPinServerRx = 2;
static constexpr int kPinClientTx = 4;
static constexpr int kPinClientRx = 2;
static constexpr uint32_t kBaud = 5000000;
static constexpr roo_transport::LinkBufferSize kWindow =
    roo_transport::kBufferSize2KB;

std::unique_ptr<roo_transport::ReliableSerial1> server_uart;
std::unique_ptr<roo_transport::ReliableSerial2> client_uart;

// Software rings must be sized before begin(). The lower RX FIFO threshold
// leaves room for bytes arriving while the interrupt is waiting to run at
// 5 Mbaud; a larger software ring cannot recover a hardware FIFO overflow.
void ConfigureUart(HardwareSerial &uart, int rx_pin, int tx_pin) {
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
    RamSnapshot("uart_driver");
    // Pre-create the same Arduino receive task to isolate its allocation.
    Serial2.onReceive([]() {});
    RamSnapshot("uart_event");
    client_uart.reset(new roo_transport::ReliableSerial2(kWindow, kWindow));
    RamSnapshot("transport_object");
    client_uart->begin(SenderAttributes("cli-send"));
    RamSnapshot("sender_worker");
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
} // namespace

#elif defined(ARDUINO_ARCH_RP2040) && defined(PICO_RP2040) && PICO_RP2040 &&   \
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
void ConfigureUart(uart_inst_t *uart, int rx_pin, int tx_pin) {
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
} // namespace

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
} // namespace

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
} // namespace
#endif

namespace {
// The server reads a length, sends that many pattern bytes, then waits for the
// next length. A varint encodes a request for one response byte in one byte.
void RunServer() {
  roo_transport::Link link = server_uart->transport().connectOrDie();
  RamSnapshot("link_connected");
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  RamSnapshot("io_readers_writers");
  while (true) {
    uint64_t remaining = in.readVarU64();
    if (in.status() == roo_io::kEndOfStream)
      break;
    CHECK_EQ(in.status(), roo_io::kOk);
    while (remaining != 0) {
      const size_t count = std::min<uint64_t>(remaining, kChunkSize);
      out.writeByteArray(reinterpret_cast<const roo::byte *>(pattern), count);
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
void LatencyTest(roo_io::InputStreamReader &in,
                 roo_io::OutputStreamWriter &out) {
  std::unique_ptr<float[]> rtt(new float[kLatencySamples]);
  RamSnapshot("latency_buffer");
  for (size_t sample = 0; sample < kLatencySamples; ++sample) {
    const roo_time::Uptime start = roo_time::Uptime::Now();
    out.writeVarU64(1);
    out.flush(); // Send this small request now, rather than filling a packet.
    CHECK_EQ(in.readU8(), pattern[0]);
    CHECK_EQ(in.status(), roo_io::kOk);
    rtt[sample] = (roo_time::Uptime::Now() - start).inMillisFloat();
  }
  std::sort(rtt.get(), rtt.get() + kLatencySamples);
  Serial.printf(
      "LATENCY role=client min_ms=%f p50_ms=%f p90_ms=%f p99_ms=%f max_ms=%f\n",
      rtt[0], rtt[kLatencySamples / 2], rtt[kLatencySamples * 9 / 10],
      rtt[kLatencySamples * 99 / 100], rtt[kLatencySamples - 1]);
}

// Request one large response and read it in small reusable blocks. Count only
// useful payload bits: 8N1 already uses ten wire bits per byte, and framing,
// ACKs and retries use more. Byte validation is included in the timed interval.
void ThroughputTest(roo_io::InputStreamReader &in,
                    roo_io::OutputStreamWriter &out, uint32_t payload_bytes) {
  // Use the heap so increasing the block size does not exhaust Pico's small
  // setup()/loop() stack. There is no allocation per received block.
  std::unique_ptr<uint8_t[]> received(new uint8_t[kChunkSize]);
  RamSnapshot("payload_buffer");
  uint32_t remaining = payload_bytes;
  const roo_time::Uptime start = roo_time::Uptime::Now();
  out.writeVarU64(payload_bytes);
  out.flush();
  while (remaining != 0) {
    const size_t wanted = std::min<uint32_t>(remaining, kChunkSize);
    const size_t count =
        in.readByteArray(reinterpret_cast<roo::byte *>(received.get()), wanted);
    CHECK_EQ(count, wanted);
    CHECK_EQ(memcmp(received.get(), pattern, count), 0);
    remaining -= count;
  }
  const uint64_t elapsed = (roo_time::Uptime::Now() - start).inMicros();
  CHECK_EQ(in.status(), roo_io::kOk);
  CHECK_EQ(out.status(), roo_io::kOk);
  static uint32_t sample = 0;
  Serial.printf(
      "RESULT role=client sample=%lu bytes=%lu us=%llu mbps=%.6f bad=0\n",
      static_cast<unsigned long>(sample++),
      static_cast<unsigned long>(payload_bytes),
      static_cast<unsigned long long>(elapsed), payload_bytes * 8.0 / elapsed);
}

// Keep readers/writers inside their Link's lifetime. A finite client closes
// its requests and waits for response EOF before allowing transport shutdown.
void RunClient(uint32_t runs, uint32_t payload_bytes) {
  roo_transport::Link link = client_uart->transport().connectOrDie();
  RamSnapshot("link_connected");
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  RamSnapshot("io_readers_writers");
  for (uint32_t run = 0; runs == 0 || run < runs; ++run) {
    LatencyTest(in, out);
    ThroughputTest(in, out, payload_bytes);
  }
  out.close();
  uint8_t final_byte;
  CHECK_EQ(in.readByteArray(reinterpret_cast<roo::byte *>(&final_byte), 1), 0u);
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
  uint32_t actual_baud = 0;
  CHECK_EQ(uart_get_baudrate(UART_NUM_2, &actual_baud), ESP_OK);
  TaskHandle_t sender_task = xTaskGetHandle("cli-send");
  CHECK(sender_task != nullptr);
  Serial.printf(
      "SYSTEM role=client chip=%s revision=%u sdk=%s cpu=%lu apb=%lu\n",
      ESP.getChipModel(), ESP.getChipRevision(), ESP.getSdkVersion(),
      static_cast<unsigned long>(getCpuFrequencyMhz() * 1000000),
      static_cast<unsigned long>(getApbFrequency()));
  Serial.printf("CONFIG role=client baud=%lu actual=%lu tx=4 rx=2 window=3 "
                "bytes=%lu runs=%lu latency_samples=%u chunk=%u app_prio=%u "
                "sender_prio=%u "
                "rxbuf=256 txbuf=1024 rx_fifo=64 rx_timeout=1\n",
                static_cast<unsigned long>(kBaud),
                static_cast<unsigned long>(actual_baud),
                static_cast<unsigned long>(kPayloadBytes),
                static_cast<unsigned long>(kRuns),
                static_cast<unsigned>(kLatencySamples),
                static_cast<unsigned>(kChunkSize),
                static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
                static_cast<unsigned>(uxTaskPriorityGet(sender_task)));
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
  RamStackWatermarks();
  RamSnapshot("links_closed");
  StopEndpoints();
}
} // namespace

void setup() {
  Serial.begin(115200);
  while (kWaitForSerial && !static_cast<bool>(Serial))
    delay(10);
  Serial.println("READY role=client");
  while (Serial.read() != 'r')
    delay(1);
  Serial.printf(
      "RAMSIZE role=client endpoint=%u out_buffer=%u in_buffer=%u channel=%u "
      "link=%u mutex=%u cv=%u static_semaphore=%u static_task=%u "
      "ring_control=%u uart_event=%u pattern=%u\n",
      static_cast<unsigned>(sizeof(*server_uart)),
      static_cast<unsigned>(sizeof(roo_transport::internal::OutBuffer)),
      static_cast<unsigned>(sizeof(roo_transport::internal::InBuffer)),
      static_cast<unsigned>(sizeof(roo_transport::Channel)),
      static_cast<unsigned>(sizeof(roo_transport::Link)),
      static_cast<unsigned>(sizeof(roo::mutex)),
      static_cast<unsigned>(sizeof(roo::condition_variable)),
      static_cast<unsigned>(sizeof(StaticSemaphore_t)),
      static_cast<unsigned>(sizeof(StaticTask_t)),
      static_cast<unsigned>(sizeof(StaticRingbuffer_t)),
      static_cast<unsigned>(sizeof(uart_event_t)),
      static_cast<unsigned>(sizeof(pattern)));
  delay(100);
  RamSnapshot("baseline");
  CHECK_EQ(heap_caps_monitor_local_minimum_free_size_start(), ESP_OK);
  RunBenchmark(kRuns, kPayloadBytes);
  delay(250);
  RamSnapshot("stopped_after_idle");
  CHECK_EQ(heap_caps_monitor_local_minimum_free_size_stop(), ESP_OK);
  Serial.println("LIFECYCLE role=client stream_eof_and_stop=PASS");
  Serial.println("DONE role=client");
}

// Continuous runs stay in RunClient(). Finite runs reach the idle Arduino loop.
void loop() { delay(1000); }
