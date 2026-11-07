#include <Arduino.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_threads.h"
#include "roo_threads/impl/freertos/config.h"
#include "roo_transport/link/link.h"
#include "roo_transport/link/link_transport.h"

#ifndef STACK_BYTES
#define STACK_BYTES 2048
#endif

#if !defined(MLOG_roo_transport_reliable_channel_connection) || \
    !MLOG_roo_transport_reliable_channel_connection
#error "Build with -DMLOG_roo_transport_reliable_channel_connection=1"
#endif

#if defined(ESP32) || defined(ESP_PLATFORM)
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#include "roo_transport/link/arduino/reliable_serial.h"

#define STACK_PROBE_ESP32 1
#define STACK_PROBE_PICO 0
static_assert(ARDUINO_SERIAL_EVENT_TASK_STACK_SIZE == STACK_BYTES,
              "Set ARDUINO_SERIAL_EVENT_TASK_STACK_SIZE to STACK_BYTES");
#elif defined(ARDUINO_ARCH_RP2040) && defined(PICO_RP2040) && PICO_RP2040 && \
    defined(__FREERTOS) && !defined(ROO_TESTING)
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <pico/platform.h>

#include "roo_transport/link/pico/reliable_uart_dma.h"

#define STACK_PROBE_ESP32 0
#define STACK_PROBE_PICO 1
static_assert(
    ROO_THREADS_FREERTOS_DEFAULT_THREAD_STACK_SIZE == STACK_BYTES,
    "Set ROO_THREADS_FREERTOS_DEFAULT_THREAD_STACK_SIZE to STACK_BYTES");
#if !defined(configUSE_CORE_AFFINITY) || !configUSE_CORE_AFFINITY
#error "The Pico probe needs task affinity to preserve DMA IRQ ownership"
#endif
#else
#error "Use native ESP32 or native RP2040 Arduino-Pico FreeRTOS"
#endif

namespace {
enum class Role : uint32_t {
  kLoopback = 0,
  kServer = 1,
  kClient = 2,
  kInternalMatrix = 3
};

struct Configuration {
  Role role = Role::kLoopback;
  uint32_t baud = 1000000;
  uint32_t window_log = 3;
  uint32_t payload_bytes = 1048577;
  uint32_t runs = 3;
};

constexpr uint32_t kWorkerStackBytes = STACK_BYTES;
constexpr uint32_t kApplicationStackBytes = 8192;
constexpr uint32_t kServerStackBytes = 8192;
constexpr UBaseType_t kApplicationPriority = 1;
constexpr size_t kLatencySamples = 100;
constexpr size_t kBulkChunk = 256;
constexpr size_t kTinyChunk = 7;
constexpr uint32_t kTinyPayloadBytes = 4099;
static_assert(kWorkerStackBytes >= 1024 && kWorkerStackBytes % 4 == 0,
              "Use a word-aligned worker stack of at least1024 bytes");

#if STACK_PROBE_ESP32
constexpr int kPrimaryTx = 4;
constexpr int kPrimaryRx = 2;
constexpr int kSecondaryTx = 17;
constexpr int kSecondaryRx = 16;
std::unique_ptr<roo_transport::ReliableSerial1> primary_uart;
std::unique_ptr<roo_transport::ReliableSerial2> secondary_uart;
#else
constexpr int kPrimaryTx = 12;
constexpr int kPrimaryRx = 13;
constexpr int kSecondaryTx = 4;
constexpr int kSecondaryRx = 5;
using DmaUart = roo_transport::pico::ReliableUartDma;
alignas(DmaUart::kRxBufferSize) uint8_t primary_rx_ring[DmaUart::kRxBufferSize];
alignas(DmaUart::kRxBufferSize) uint8_t
    secondary_rx_ring[DmaUart::kRxBufferSize];
std::unique_ptr<DmaUart> primary_uart;
std::unique_ptr<DmaUart> secondary_uart;
uint32_t initial_primary_errors = 0;
uint32_t initial_secondary_errors = 0;
#endif

Configuration configuration;
roo::thread server_thread;
std::atomic<uint32_t> disconnect_callbacks{0};
uint32_t owner_core = 0;
uint32_t actual_primary_baud = 0;
uint32_t actual_secondary_baud = 0;

// Both loopback topologies run the same protocol and measurement workload.
bool IsLoopback() {
  return configuration.role == Role::kLoopback ||
         configuration.role == Role::kInternalMatrix;
}

const char *TopologyName() {
  if (configuration.role == Role::kInternalMatrix) return "internal_matrix";
  return IsLoopback() ? "crossed_wires" : "two_board";
}

// Identifies the core that must keep ownership of hardware initialization/end.
uint32_t CurrentCore() {
#if STACK_PROBE_ESP32
  return xPortGetCoreID();
#else
  return get_core_num();
#endif
}

// Uses an absolute byte index so truncated, displaced or repeated blocks fail.
uint8_t PatternByte(uint32_t index) {
  return static_cast<uint8_t>((index * 73u + 19u) ^ (index >> 8) ^
                              (index >> 16));
}

// Records peer reset without running logging or fatal handling on an RX worker.
void PeerDisconnected() {
  disconnect_callbacks.fetch_add(1, std::memory_order_relaxed);
}

roo::thread::attributes SenderAttributes(const char *name) {
  roo::thread::attributes attributes;
  attributes.set_name(name);
  attributes.set_stack_size(kWorkerStackBytes);
  attributes.set_priority(kApplicationPriority);
  return attributes;
}

roo_transport::LinkTransport &PrimaryTransport() {
  CHECK(primary_uart != nullptr);
  return primary_uart->transport();
}

roo_transport::LinkTransport &ClientTransport() {
  if (IsLoopback()) {
    CHECK(secondary_uart != nullptr);
    return secondary_uart->transport();
  }
  return PrimaryTransport();
}

#if STACK_PROBE_ESP32
// Configure the SDK driver before attaching the transport's receive callbacks.
void ConfigureUart(HardwareSerial &uart, int rx_pin, int tx_pin) {
  CHECK_EQ(uart.setRxBufferSize(256), 256u);
  CHECK_EQ(uart.setTxBufferSize(1024), 1024u);
  uart.begin(configuration.baud, SERIAL_8N1, rx_pin, tx_pin);
  CHECK(uart.setRxFIFOFull(64));
  CHECK(uart.setRxTimeout(1));
}
#else
void ConfigureUart(uart_inst_t *uart, int rx_pin, int tx_pin,
                   uint32_t &actual_baud) {
  gpio_set_function(tx_pin, GPIO_FUNC_UART);
  gpio_set_function(rx_pin, GPIO_FUNC_UART);
  actual_baud = uart_init(uart, configuration.baud);
  uart_set_format(uart, 8, 1, UART_PARITY_NONE);
}
#endif

// Cross-board roles both use their primary UART; loopback adds the secondary.
// All active GPIOs/UARTs are ready before a DMA constructor arms reception.
void InitializeEndpoints() {
  CHECK_EQ(CurrentCore(), owner_core);
  const bool loopback = IsLoopback();
  const roo_transport::LinkBufferSize window =
      static_cast<roo_transport::LinkBufferSize>(configuration.window_log);
#if STACK_PROBE_ESP32
  ConfigureUart(Serial1, kPrimaryRx, kPrimaryTx);
  if (loopback) ConfigureUart(Serial2, kSecondaryRx, kSecondaryTx);
  if (configuration.role == Role::kInternalMatrix) {
    // Preserve Arduino pin ownership while sampling each driven TX pad as the
    // other UART's RX. The external ESP32 pair wiring can remain connected.
    CHECK_EQ(gpio_input_enable(static_cast<gpio_num_t>(kPrimaryTx)), ESP_OK);
    CHECK_EQ(gpio_input_enable(static_cast<gpio_num_t>(kSecondaryTx)), ESP_OK);
    esp_rom_gpio_connect_in_signal(kPrimaryTx, U2RXD_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(kSecondaryTx, U1RXD_IN_IDX, false);
  }
  CHECK_EQ(uart_get_baudrate(UART_NUM_1, &actual_primary_baud), ESP_OK);
  if (loopback) {
    CHECK_EQ(uart_get_baudrate(UART_NUM_2, &actual_secondary_baud), ESP_OK);
  }
  primary_uart.reset(new roo_transport::ReliableSerial1(window, window));
  if (loopback) {
    secondary_uart.reset(new roo_transport::ReliableSerial2(window, window));
  }
#else
  const uint32_t system_hz = clock_get_hz(clk_sys);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  system_hz, system_hz);
  ConfigureUart(uart0, kPrimaryRx, kPrimaryTx, actual_primary_baud);
  if (loopback) {
    ConfigureUart(uart1, kSecondaryRx, kSecondaryTx, actual_secondary_baud);
  }
  primary_uart.reset(new DmaUart(uart0, primary_rx_ring, window, window));
  if (loopback) {
    secondary_uart.reset(new DmaUart(uart1, secondary_rx_ring, window, window));
  }
#endif
  primary_uart->begin(SenderAttributes("probe-tx0"));
  if (loopback) secondary_uart->begin(SenderAttributes("probe-tx1"));
  // Let RX workers account for peer GPIO setup transitions before data starts.
  delay(25);
#if STACK_PROBE_PICO
  initial_primary_errors = primary_uart->receiveErrors();
  if (loopback) initial_secondary_errors = secondary_uart->receiveErrors();
#endif
}

// A name may repeat for two receive tasks; task number disambiguates them.
bool IsWorker(const char *name) {
  if (strcmp(name, "probe-tx0") == 0 || strcmp(name, "probe-tx1") == 0) {
    return true;
  }
#if STACK_PROBE_ESP32
  return strcmp(name, "uart_event_task") == 0;
#else
  return strcmp(name, "dma-recv") == 0;
#endif
}

// Snapshot only from the large application task while the wire is idle. The
// target workers never format their own reports on their small stacks.
void SnapshotWorkers(const char *stage) {
#if defined(configUSE_TRACE_FACILITY) && configUSE_TRACE_FACILITY
  UBaseType_t capacity = uxTaskGetNumberOfTasks() + 8;
  std::unique_ptr<TaskStatus_t[]> tasks;
  UBaseType_t count = 0;
  for (unsigned attempt = 0; attempt < 3 && count == 0; ++attempt) {
    tasks.reset(new TaskStatus_t[capacity]);
    count = uxTaskGetSystemState(tasks.get(), capacity, nullptr);
    if (count == 0) capacity *= 2;
  }
  CHECK_GT(count, 0u);
  uint32_t workers = 0;
  for (UBaseType_t index = 0; index < count; ++index) {
    const TaskStatus_t &task = tasks[index];
    if (!IsWorker(task.pcTaskName)) continue;
    const uint32_t free_raw = task.usStackHighWaterMark;
    const uint32_t free_bytes = free_raw * sizeof(StackType_t);
    CHECK_LE(free_bytes, kWorkerStackBytes);
    Serial.printf(
        "STACK stage=%s name=%s number=%lu free=%lu used=%lu stack=%lu "
        "raw=%lu unit_bytes=%u\n",
        stage, task.pcTaskName, static_cast<unsigned long>(task.xTaskNumber),
        static_cast<unsigned long>(free_bytes),
        static_cast<unsigned long>(kWorkerStackBytes - free_bytes),
        static_cast<unsigned long>(kWorkerStackBytes),
        static_cast<unsigned long>(free_raw),
        static_cast<unsigned>(sizeof(StackType_t)));
    ++workers;
  }
  const uint32_t expected = IsLoopback() ? 4 : 2;
  CHECK_EQ(workers, expected);
  Serial.printf("STACK_SNAPSHOT stage=%s workers=%lu expected=%lu\n", stage,
                static_cast<unsigned long>(workers),
                static_cast<unsigned long>(expected));
#else
  Serial.printf("STACK_UNSUPPORTED stage=%s trace=0\n", stage);
#endif
}

// Reports actual wire settings and compile-time stack reservations separately.
void PrintConfiguration() {
  Serial.printf(
      "CONFIG role=%lu topology=%s baud=%lu actual=%lu actual2=%lu window=%lu "
      "bytes=%lu "
      "runs=%lu sender_stack=%lu receiver_stack=%lu app_stack=%lu "
      "server_stack=%lu unit_bytes=%u owner_core=%lu mlog=1 primary_tx=%d "
      "primary_rx=%d secondary_tx=%d secondary_rx=%d configured_primary_rx=%d "
      "configured_secondary_rx=%d\n",
      static_cast<unsigned long>(configuration.role), TopologyName(),
      static_cast<unsigned long>(configuration.baud),
      static_cast<unsigned long>(actual_primary_baud),
      static_cast<unsigned long>(actual_secondary_baud),
      static_cast<unsigned long>(configuration.window_log),
      static_cast<unsigned long>(configuration.payload_bytes),
      static_cast<unsigned long>(configuration.runs),
      static_cast<unsigned long>(kWorkerStackBytes),
      static_cast<unsigned long>(kWorkerStackBytes),
      static_cast<unsigned long>(kApplicationStackBytes),
      static_cast<unsigned long>(kServerStackBytes),
      static_cast<unsigned>(sizeof(StackType_t)),
      static_cast<unsigned long>(owner_core), kPrimaryTx,
      configuration.role == Role::kInternalMatrix ? kSecondaryTx : kPrimaryRx,
      kSecondaryTx,
      configuration.role == Role::kInternalMatrix ? kPrimaryTx : kSecondaryRx,
      kPrimaryRx, kSecondaryRx);
}

// Serves requests until request EOF, using tiny writes for the small-I/O phase.
void RunServer() {
  roo_transport::Link link = PrimaryTransport().connect(PeerDisconnected);
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  uint8_t block[kBulkChunk];
  while (true) {
    const uint64_t requested = in.readVarU64();
    if (in.status() == roo_io::kEndOfStream) break;
    CHECK(in.ok());
    CHECK_GT(requested, 0u);
    CHECK_LE(requested, UINT32_MAX);
    const uint32_t length = requested;
    uint32_t offset = 0;
    while (offset < length) {
      const size_t chunk =
          length == kTinyPayloadBytes ? 1 + offset % kTinyChunk : kBulkChunk;
      const size_t count = std::min<uint32_t>(length - offset, chunk);
      for (size_t index = 0; index < count; ++index) {
        block[index] = PatternByte(offset + index);
      }
      out.writeByteArray(reinterpret_cast<const roo::byte *>(block), count);
      offset += count;
    }
    out.flush();
    CHECK_EQ(out.status(), roo_io::kOk);
  }
  out.close();
  Serial.println("SERVER request_eof=1 response_closed=1");
}

// Time 100 one-byte replies without floating-point formatting or stack arrays.
void LatencyTest(roo_io::InputStreamReader &in, roo_io::OutputStreamWriter &out,
                 uint32_t sample) {
  std::unique_ptr<uint32_t[]> times(new uint32_t[kLatencySamples]);
  for (size_t index = 0; index < kLatencySamples; ++index) {
    const roo_time::Uptime start = roo_time::Uptime::Now();
    out.writeVarU64(1);
    out.flush();
    CHECK_EQ(in.readU8(), PatternByte(0));
    CHECK(in.ok());
    times[index] = (roo_time::Uptime::Now() - start).inMicros();
  }
  std::sort(times.get(), times.get() + kLatencySamples);
  Serial.printf(
      "LATENCY sample=%lu samples=%u min_us=%lu p50_us=%lu p90_us=%lu "
      "p99_us=%lu max_us=%lu\n",
      static_cast<unsigned long>(sample),
      static_cast<unsigned>(kLatencySamples),
      static_cast<unsigned long>(times[0]),
      static_cast<unsigned long>(times[kLatencySamples / 2]),
      static_cast<unsigned long>(times[kLatencySamples * 9 / 10]),
      static_cast<unsigned long>(times[kLatencySamples * 99 / 100]),
      static_cast<unsigned long>(times[kLatencySamples - 1]));
}

// Verify each absolute byte index, including an arbitrary final partial block.
void PayloadTest(roo_io::InputStreamReader &in, roo_io::OutputStreamWriter &out,
                 uint32_t bytes, uint32_t sample, bool tiny) {
  uint8_t block[kBulkChunk];
  uint32_t offset = 0;
  const roo_time::Uptime start = roo_time::Uptime::Now();
  out.writeVarU64(bytes);
  out.flush();
  while (offset < bytes) {
    const size_t chunk = tiny ? 1 + offset % kTinyChunk : kBulkChunk;
    const size_t wanted = std::min<uint32_t>(bytes - offset, chunk);
    const size_t count =
        in.readByteArray(reinterpret_cast<roo::byte *>(block), wanted);
    CHECK_EQ(count, wanted);
    for (size_t index = 0; index < count; ++index) {
      CHECK_EQ(block[index], PatternByte(offset + index));
    }
    offset += count;
  }
  const uint64_t elapsed = (roo_time::Uptime::Now() - start).inMicros();
  CHECK(in.ok());
  CHECK_EQ(out.status(), roo_io::kOk);
  Serial.printf("RESULT kind=%s sample=%lu bytes=%lu us=%llu bad=0\n",
                tiny ? "tiny" : "bulk", static_cast<unsigned long>(sample),
                static_cast<unsigned long>(bytes),
                static_cast<unsigned long long>(elapsed));
}

void RunClient() {
  roo_transport::Link link = ClientTransport().connect(PeerDisconnected);
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  for (uint32_t run = 0; run < configuration.runs; ++run) {
    LatencyTest(in, out, run);
    PayloadTest(in, out, kTinyPayloadBytes, run, true);
    PayloadTest(in, out, configuration.payload_bytes, run, false);
  }
  out.close();
  uint8_t final_byte;
  CHECK_EQ(in.readByteArray(reinterpret_cast<roo::byte *>(&final_byte), 1), 0u);
  CHECK_EQ(in.status(), roo_io::kEndOfStream);
  Serial.println("CLIENT request_closed=1 response_eof=1");
}

// Every Link/adapter and the server application thread have finished by here.
void StopEndpoints() {
  CHECK_EQ(CurrentCore(), owner_core);
#if STACK_PROBE_PICO
  Serial.printf(
      "DMA overruns1=%lu errors1=%lu startup_errors1=%lu overruns2=%lu "
      "errors2=%lu startup_errors2=%lu\n",
      static_cast<unsigned long>(primary_uart->receiveOverruns()),
      static_cast<unsigned long>(primary_uart->receiveErrors()),
      static_cast<unsigned long>(initial_primary_errors),
      static_cast<unsigned long>(
          secondary_uart != nullptr ? secondary_uart->receiveOverruns() : 0),
      static_cast<unsigned long>(
          secondary_uart != nullptr ? secondary_uart->receiveErrors() : 0),
      static_cast<unsigned long>(initial_secondary_errors));
  CHECK_EQ(primary_uart->receiveOverruns(), 0u);
  CHECK_EQ(primary_uart->receiveErrors(), initial_primary_errors);
  if (secondary_uart != nullptr) {
    CHECK_EQ(secondary_uart->receiveOverruns(), 0u);
    CHECK_EQ(secondary_uart->receiveErrors(), initial_secondary_errors);
  }
#endif
  primary_uart->end();
  primary_uart.reset();
  if (secondary_uart != nullptr) {
    secondary_uart->end();
    secondary_uart.reset();
  }
#if STACK_PROBE_ESP32
  Serial1.end();
  if (IsLoopback()) Serial2.end();
#endif
  Serial.printf("LIFECYCLE stream_eof_and_stop=PASS callbacks=%lu\n",
                static_cast<unsigned long>(disconnect_callbacks.load()));
}

// The caller uses a dedicated 8 KiB task; Pico's built-in setup stack is4 KiB.
void RunProbe() {
  InitializeEndpoints();
  PrintConfiguration();
  SnapshotWorkers("started");
  if (IsLoopback()) {
    roo::thread::attributes attributes;
    attributes.set_name("probe-srv");
    attributes.set_stack_size(kServerStackBytes);
    attributes.set_priority(kApplicationPriority);
    server_thread = roo::thread(attributes, RunServer);
    RunClient();
    server_thread.join();
  } else if (configuration.role == Role::kServer) {
    RunServer();
  } else {
    RunClient();
  }
  delay(25);
  SnapshotWorkers("finished");
  StopEndpoints();
  Serial.println("DONE");
  Serial.flush();
}

// Signal only after hardware cleanup; the setup task owns no borrowed context.
void ProbeTask(void *setup_task) {
  CHECK_EQ(CurrentCore(), owner_core);
  RunProbe();
  xTaskNotifyGive(static_cast<TaskHandle_t>(setup_task));
  vTaskDelete(nullptr);
}

// Parse the complete finite request before changing any UART/hardware state.
bool ReadConfiguration() {
  char command[160];
  size_t length = 0;
  while (true) {
    if (Serial.available() == 0) {
      delay(1);
      continue;
    }
    const int value = Serial.read();
    if (value == '\r') continue;
    if (value == '\n') break;
    if (length + 1 >= sizeof(command)) return false;
    command[length++] = static_cast<char>(value);
  }
  command[length] = '\0';
  char verb[8];
  uint32_t role;
  char extra;
  const int fields = sscanf(
      command,
      "%7s %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %c",
      verb, &role, &configuration.baud, &configuration.window_log,
      &configuration.payload_bytes, &configuration.runs, &extra);
  if (fields != 6 || strcmp(verb, "run") != 0 || role > 3 ||
      configuration.baud == 0 || configuration.window_log > 10 ||
      configuration.payload_bytes == 0 || configuration.runs == 0 ||
      configuration.runs > 1000) {
    return false;
  }
#if !STACK_PROBE_ESP32
  if (role == 3) return false;
#endif
  configuration.role = static_cast<Role>(role);
  return true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
#if STACK_PROBE_PICO
  while (!static_cast<bool>(Serial)) delay(10);
#else
  delay(250);
#endif
  owner_core = CurrentCore();
  Serial.println(
      "READY stack_probe=1 command=run_ROLE_BAUD_WINDOW_LOG_BYTES_RUNS");
  if (!ReadConfiguration()) {
    Serial.println("ERROR configuration=1");
    return;
  }
  const TaskHandle_t setup_task = xTaskGetCurrentTaskHandle();
#if STACK_PROBE_ESP32
  CHECK_EQ(xTaskCreatePinnedToCore(ProbeTask, "probe-app",
                                   kApplicationStackBytes, setup_task,
                                   kApplicationPriority, nullptr, owner_core),
           pdPASS);
#else
  CHECK_EQ(xTaskCreateAffinitySet(ProbeTask, "probe-app",
                                  kApplicationStackBytes / sizeof(StackType_t),
                                  setup_task, kApplicationPriority,
                                  static_cast<UBaseType_t>(1u << owner_core),
                                  nullptr),
           pdPASS);
#endif
  CHECK_EQ(ulTaskNotifyTake(pdTRUE, portMAX_DELAY), 1u);
}

void loop() { delay(1000); }
