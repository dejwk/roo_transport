#include <Arduino.h>
#include <driver/uart.h>
#include <esp_rom_gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>

#include <algorithm>
#include <atomic>
#include <memory>

#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_threads/thread.h"
#include "roo_transport/link/arduino/reliable_serial.h"

#if SOC_UART_NUM < 3
#error "This loopback benchmark requires UART1 and UART2."
#endif

namespace {
constexpr int kServerTx = 4;
constexpr int kServerRx = 2;
constexpr int kClientTx = 17;
constexpr int kClientRx = 16;
constexpr size_t kMaxChunk = 4096;
constexpr size_t kLatencySamples = 100;

uint32_t baud = 5000000;
uint32_t rx_buffer_size = 8192;
uint32_t tx_buffer_size = 0;
uint32_t window_log = 6;
uint32_t mode = 0;
uint32_t transfer_size = 1048576;
uint32_t repeats = 5;
uint32_t application_priority = 0;
uint32_t sender_priority = 0;
uint32_t rx_fifo_threshold = 120;
uint32_t rx_timeout_symbols = 2;
uint32_t chunk_size = 256;
uint32_t split_irq = 0;
uint32_t fault_us = 0;
uint32_t irq1_core = 0;
uint32_t irq2_core = 0;
uint8_t pattern[kMaxChunk];
uint8_t received[kMaxChunk];
std::unique_ptr<roo_transport::ReliableSerial1> server_transport;
std::unique_ptr<roo_transport::ReliableSerial2> client_transport;
roo::thread server_thread;
SemaphoreHandle_t raw_start = nullptr;
std::atomic<bool> raw_done{false};
std::atomic<bool> measuring{false};
std::atomic<uint32_t> last_progress_ms{0};
std::atomic<uint32_t> received_payload_bytes{0};
std::atomic<bool> fault_applied{false};
std::atomic<bool> fault_restored{false};
constexpr size_t kErrorTypeCount = UART_PARITY_ERROR + 1;
std::atomic<uint32_t> uart_errors[2];
std::atomic<uint32_t> uart_error_types[2][kErrorTypeCount];
std::atomic<uint32_t> uart_other_errors[2];

// Records delivered progress without adding console traffic to timed transfers.
void Progress() { last_progress_ms.store(millis(), std::memory_order_relaxed); }

// Recovers stalled runs while keeping successful measurements independent of
// task-watchdog configuration and without printing inside the hot loop.
void Monitor(void*) {
  while (measuring.load()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (millis() - last_progress_ms.load(std::memory_order_relaxed) > 20000) {
      Serial.println("ERROR stalled=1");
      Serial.flush();
      ESP.restart();
    }
  }
  vTaskDelete(nullptr);
}

// Classifies observed receive errors without printing inside UART callbacks.
void RecordUartError(size_t port, hardwareSerial_error_t error) {
  ++uart_errors[port];
  const size_t type = static_cast<size_t>(error);
  if (type > UART_NO_ERROR && type < kErrorTypeCount) {
    ++uart_error_types[port][type];
  } else {
    ++uart_other_errors[port];
  }
}

// Reports startup and measured errors separately so initialization transitions
// are visible without being attributed to an otherwise clean timed transfer.
void PrintErrorTypes(const char* record) {
  for (size_t port = 0; port < 2; ++port) {
    Serial.printf("%s observed=%d uart=%u total=%" PRIu32 " break=%" PRIu32
                  " buffer=%" PRIu32 " fifo=%" PRIu32 " frame=%" PRIu32
                  " parity=%" PRIu32 " other=%" PRIu32 "\n",
                  record, mode != 0, static_cast<unsigned int>(port + 1),
                  uart_errors[port].load(),
                  uart_error_types[port][UART_BREAK_ERROR].load(),
                  uart_error_types[port][UART_BUFFER_FULL_ERROR].load(),
                  uart_error_types[port][UART_FIFO_OVF_ERROR].load(),
                  uart_error_types[port][UART_FRAME_ERROR].load(),
                  uart_error_types[port][UART_PARITY_ERROR].load(),
                  uart_other_errors[port].load());
  }
}

// The crossed RX pin can see a framing transition while the peer UART's TX is
// not initialized. Let its pending events run, flush both RX paths, then start
// measured error accounting only after the initialized lines have settled.
void SettleUarts() {
  delay(25);
  CHECK_EQ(uart_flush_input(UART_NUM_1), ESP_OK);
  CHECK_EQ(uart_flush_input(UART_NUM_2), ESP_OK);
  Serial1.eventQueueReset();
  Serial2.eventQueueReset();
  delay(25);
  PrintErrorTypes("STARTUP_ERRORS");
  for (size_t port = 0; port < 2; ++port) {
    uart_errors[port] = 0;
    uart_other_errors[port] = 0;
    for (size_t type = 0; type < kErrorTypeCount; ++type) {
      uart_error_types[port][type] = 0;
    }
  }
}

// Installs UART1 and its raw error callback on the calling core. The IDF
// driver allocates its UART interrupt on the core that calls begin().
void ConfigureUart1() {
  CHECK_EQ(Serial1.setRxBufferSize(rx_buffer_size), rx_buffer_size);
  CHECK_EQ(Serial1.setTxBufferSize(tx_buffer_size), tx_buffer_size);
  irq1_core = xPortGetCoreID();
  Serial1.begin(baud, SERIAL_8N1, kServerRx, kServerTx);
  CHECK(Serial1.setRxFIFOFull(rx_fifo_threshold));
  CHECK(Serial1.setRxTimeout(rx_timeout_symbols));
  if (mode != 0) {
    Serial1.onReceiveError(
        [](hardwareSerial_error_t error) { RecordUartError(0, error); });
  }
}

// Installs UART2 on the application core for the crossed-wire peer.
void ConfigureUart2() {
  CHECK_EQ(Serial2.setRxBufferSize(rx_buffer_size), rx_buffer_size);
  CHECK_EQ(Serial2.setTxBufferSize(tx_buffer_size), tx_buffer_size);
  irq2_core = xPortGetCoreID();
  Serial2.begin(baud, SERIAL_8N1, kClientRx, kClientTx);
  CHECK(Serial2.setRxFIFOFull(rx_fifo_threshold));
  CHECK(Serial2.setRxTimeout(rx_timeout_symbols));
  if (mode != 0) {
    Serial2.onReceiveError(
        [](hardwareSerial_error_t error) { RecordUartError(1, error); });
  }
}

// Notifies the main task after initialization without borrowing a temporary
// semaphore or stack context that main could destroy before this task returns.
void ConfigureUart1Task(void* main_task) {
  ConfigureUart1();
  xTaskNotifyGive(static_cast<TaskHandle_t>(main_task));
  vTaskDelete(nullptr);
}

// Selects shared application-core IRQ ownership or splits UART1 onto core0.
// The temporary task uses notification slot0 before any transport/CV starts.
void ConfigureUarts() {
  if (split_irq != 0) {
    CHECK_EQ(
        xTaskCreatePinnedToCore(ConfigureUart1Task, "uart1-init", 4096,
                                xTaskGetCurrentTaskHandle(), 1, nullptr, 0),
        pdPASS);
    CHECK_EQ(ulTaskNotifyTake(pdTRUE, portMAX_DELAY), 1u);
  } else {
    ConfigureUart1();
  }
  ConfigureUart2();
  uint32_t actual1 = 0;
  uint32_t actual2 = 0;
  CHECK_EQ(uart_get_baudrate(UART_NUM_1, &actual1), ESP_OK);
  CHECK_EQ(uart_get_baudrate(UART_NUM_2, &actual2), ESP_OK);
  Serial.printf("SYSTEM chip=%s revision=%u sdk=%s\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getSdkVersion());
  Serial.printf(
      "CONFIG baud=%" PRIu32 " actual=%" PRIu32 " actual2=%" PRIu32
      " cpu=%" PRIu32 " apb=%" PRIu32 " rxbuf=%" PRIu32 " txbuf=%" PRIu32
      " window=%" PRIu32 " mode=%" PRIu32 " bytes=%" PRIu32 " repeats=%" PRIu32
      " app_prio=%" PRIu32 " rx_fifo=%" PRIu32 " rx_timeout=%" PRIu32
      " chunk=%" PRIu32 " split_irq=%" PRIu32 " fault_us=%" PRIu32
      " sender_prio=%" PRIu32 " irq1_core=%" PRIu32 " irq2_core=%" PRIu32
      " cores=%u tick=%u\n",
      baud, actual1, actual2,
      static_cast<uint32_t>(getCpuFrequencyMhz() * 1000000), getApbFrequency(),
      rx_buffer_size, tx_buffer_size, window_log, mode, transfer_size, repeats,
      application_priority, rx_fifo_threshold, rx_timeout_symbols, chunk_size,
      split_irq, fault_us, sender_priority, irq1_core, irq2_core,
      portNUM_PROCESSORS, configTICK_RATE_HZ);
}

// Serves the finite latency and throughput request counts, then sends EOF.
void Server() {
  roo_transport::LinkStream link = server_transport->connectOrDie();
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  for (uint32_t request = 0; request < kLatencySamples + repeats; ++request) {
    uint32_t remaining = in.readVarU64();
    CHECK_EQ(in.status(), roo_io::kOk);
    while (remaining != 0) {
      const size_t count = std::min<uint32_t>(remaining, chunk_size);
      out.writeByteArray(reinterpret_cast<const roo::byte*>(pattern), count);
      remaining -= count;
    }
    out.flush();
  }
  out.close();
}

// Runs a producer at the requested priority; zero preserves the example's
// low-priority server while leaving the Arduino main task priority unchanged.
void StartServer(void (*function)()) {
  roo::thread::attributes attrs;
  attrs.set_name("bench-server");
  attrs.set_stack_size(8192);
  attrs.set_priority(application_priority == 0 ? 1 : application_priority);
  server_thread = roo::thread(attrs, function);
}

// Creates a receive break after verified payload progress without changing any
// GPIO output. Restore the complete matrix register, including inversion and
// IOMUX/matrix selection, before letting normal driver teardown proceed.
void InjectFault(void*) {
  while (received_payload_bytes.load(std::memory_order_relaxed) < 32768) {
    vTaskDelay(1);
  }
  const uint32_t saved = GPIO.func_in_sel_cfg[U2RXD_IN_IDX].val;
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, U2RXD_IN_IDX,
                                 false);
  fault_applied = true;
  const uint64_t ticks =
      (static_cast<uint64_t>(fault_us) * configTICK_RATE_HZ + 999999) / 1000000;
  vTaskDelay(std::max<TickType_t>(1, static_cast<TickType_t>(ticks)));
  GPIO.func_in_sel_cfg[U2RXD_IN_IDX].val = saved;
  CHECK_EQ(GPIO.func_in_sel_cfg[U2RXD_IN_IDX].val, saved);
  fault_restored = true;
  vTaskDelete(nullptr);
}

// Keep --transport-ref usable with revisions predating sender attributes.
// Such references can benchmark their original defaults, but cannot apply an
// explicit sender configuration that their public API does not support.
template <typename Endpoint>
auto BeginWithSenderAttributes(Endpoint& endpoint,
                               const roo::thread::attributes& attributes, int)
    -> decltype(endpoint.begin(attributes), void()) {
  endpoint.begin(attributes);
}

template <typename Endpoint>
void BeginWithSenderAttributes(Endpoint&, const roo::thread::attributes&,
                               long) {
  LOG(FATAL) << "Selected transport revision has no sender-attribute API";
}

// Validates complete ordered payloads and correct stream/worker shutdown.
void LinkTests() {
  const auto window = static_cast<roo_transport::LinkBufferSize>(window_log);
  server_transport.reset(new roo_transport::ReliableSerial1(window, window));
  client_transport.reset(new roo_transport::ReliableSerial2(window, window));
  if (sender_priority == 0) {
    server_transport->begin();
    client_transport->begin();
  } else {
    roo::thread::attributes sender_attrs;
    sender_attrs.set_stack_size(4096);
    sender_attrs.set_priority(sender_priority);
    sender_attrs.set_name("serial1-send");
    BeginWithSenderAttributes(*server_transport, sender_attrs, 0);
    sender_attrs.set_name("serial2-send");
    BeginWithSenderAttributes(*client_transport, sender_attrs, 0);
  }
  TaskHandle_t sender1 = xTaskGetHandle("serial1-send");
  TaskHandle_t sender2 = xTaskGetHandle("serial2-send");
  CHECK(sender1 != nullptr);
  CHECK(sender2 != nullptr);
  Serial.printf("SENDER_PRIORITY sender1=%u sender2=%u\n",
                static_cast<unsigned int>(uxTaskPriorityGet(sender1)),
                static_cast<unsigned int>(uxTaskPriorityGet(sender2)));
  StartServer(Server);
  {
    roo_transport::LinkStream link = client_transport->connectOrDie();
    roo_io::InputStreamReader in(link.in());
    roo_io::OutputStreamWriter out(link.out());
    Serial.println("CONNECTED");
    uint32_t rtt[kLatencySamples];
    for (size_t sample = 0; sample < kLatencySamples; ++sample) {
      const int64_t start = esp_timer_get_time();
      out.writeVarU64(1);
      out.flush();
      CHECK_EQ(in.readU8(), pattern[0]);
      rtt[sample] = esp_timer_get_time() - start;
      Progress();
    }
    std::sort(rtt, rtt + kLatencySamples);
    Serial.printf("LATENCY min=%" PRIu32 " p50=%" PRIu32 " p90=%" PRIu32
                  " p99=%" PRIu32 " max=%" PRIu32 "\n",
                  rtt[0], rtt[50], rtt[90], rtt[99], rtt[99]);
    if (fault_us != 0) {
      CHECK_EQ(
          xTaskCreatePinnedToCore(InjectFault, "bench-fault", 2048, nullptr,
                                  configMAX_PRIORITIES - 1, nullptr, 0),
          pdPASS);
    }
    auto stats = server_transport->statsMonitor();
    for (uint32_t sample = 0; sample < repeats; ++sample) {
      delay(25);
      const uint32_t sent_before = stats.packets_sent();
      const uint32_t delivered_before = stats.packets_delivered();
      uint32_t remaining = transfer_size;
      uint32_t bad = 0;
      const int64_t start = esp_timer_get_time();
      out.writeVarU64(transfer_size);
      out.flush();
      while (remaining != 0) {
        const size_t count =
            in.readByteArray(reinterpret_cast<roo::byte*>(received),
                             std::min<uint32_t>(remaining, chunk_size));
        CHECK_GT(count, 0u);
        for (size_t i = 0; i < count; ++i) {
          if (received[i] !=
              pattern[(transfer_size - remaining + i) % kMaxChunk]) {
            ++bad;
          }
        }
        remaining -= count;
        received_payload_bytes.fetch_add(count, std::memory_order_relaxed);
        Progress();
      }
      const int64_t elapsed = esp_timer_get_time() - start;
      delay(25);
      Serial.printf(
          "RESULT kind=link sample=%" PRIu32 " bytes=%" PRIu32 " us=%" PRId64
          " mbps=%.6f bad=%" PRIu32 " sent=%" PRIu32 " delivered=%" PRIu32 "\n",
          sample, transfer_size, elapsed, transfer_size * 8.0 / elapsed, bad,
          stats.packets_sent() - sent_before,
          stats.packets_delivered() - delivered_before);
      CHECK_EQ(bad, 0u);
      CHECK_EQ(in.status(), roo_io::kOk);
      CHECK_EQ(out.status(), roo_io::kOk);
      Progress();
    }
    CHECK_EQ(in.readByteArray(reinterpret_cast<roo::byte*>(received), 1), 0u);
    CHECK_EQ(in.status(), roo_io::kEndOfStream);
    out.close();
    server_thread.join();
  }
  if (fault_us != 0) {
    while (!fault_restored.load()) vTaskDelay(1);
    CHECK(fault_applied.load());
  }
  delay(25);
  server_transport->end();
  client_transport->end();
  server_transport.reset();
  client_transport.reset();
  Serial.println("LIFECYCLE stream_eof_and_stop=PASS");
}

// Writes raw pattern bytes using the selected Arduino or IDF UART API.
void RawServer() {
  const uart_port_t transmit_port = mode == 3 ? UART_NUM_2 : UART_NUM_1;
  for (uint32_t sample = 0; sample < repeats; ++sample) {
    xSemaphoreTake(raw_start, portMAX_DELAY);
    uint32_t remaining = transfer_size;
    while (remaining != 0) {
      const size_t count = std::min<uint32_t>(remaining, chunk_size);
      if (mode == 1) {
        CHECK_EQ(Serial1.write(pattern, count), count);
      } else {
        CHECK_EQ(uart_write_bytes(transmit_port, pattern, count),
                 static_cast<int>(count));
      }
      remaining -= count;
    }
    if (mode == 1) {
      Serial1.flush();
    } else {
      CHECK_EQ(uart_wait_tx_done(transmit_port, portMAX_DELAY), ESP_OK);
    }
    raw_done.store(true, std::memory_order_release);
  }
}

// Isolates physical wire/API throughput from packet framing and protocol work.
void RawTests() {
  const uart_port_t receive_port = mode == 3 ? UART_NUM_1 : UART_NUM_2;
  raw_start = xSemaphoreCreateBinary();
  CHECK(raw_start != nullptr);
  StartServer(RawServer);
  for (uint32_t sample = 0; sample < repeats; ++sample) {
    raw_done.store(false, std::memory_order_release);
    uint32_t total = 0;
    uint32_t bad = 0;
    uint32_t first_bad = UINT32_MAX;
    uint32_t first_actual = 0;
    uint32_t first_expected = 0;
    int64_t last_data = esp_timer_get_time();
    const int64_t start = last_data;
    xSemaphoreGive(raw_start);
    while (total < transfer_size) {
      size_t available = 0;
      if (mode == 1) {
        available = Serial2.available();
      } else {
        CHECK_EQ(uart_get_buffered_data_len(receive_port, &available), ESP_OK);
      }
      if (available > 0) {
        const size_t wanted = std::min<size_t>(
            std::min<size_t>(available, transfer_size - total), chunk_size);
        size_t count;
        if (mode == 1) {
          count = Serial2.readBytes(received, wanted);
        } else {
          const int read = uart_read_bytes(receive_port, received, wanted, 0);
          CHECK_GE(read, 0);
          count = static_cast<size_t>(read);
        }
        for (size_t i = 0; i < count; ++i) {
          const uint8_t expected = pattern[(total + i) % kMaxChunk];
          if (received[i] != expected) {
            if (first_bad == UINT32_MAX) {
              first_bad = total + i;
              first_actual = received[i];
              first_expected = expected;
            }
            ++bad;
          }
        }
        total += count;
        if (count > 0) {
          last_data = esp_timer_get_time();
          Progress();
        }
      } else if (raw_done.load(std::memory_order_acquire) &&
                 esp_timer_get_time() - last_data > 100000) {
        break;
      } else {
        vTaskDelay(1);
      }
    }
    const int64_t elapsed = last_data - start;
    if (bad != 0 || total != transfer_size) {
      delay(2);
      PrintErrorTypes("TRANSFER_ERRORS");
      Serial.printf("MISMATCH offset=%" PRIu32 " actual=%" PRIu32
                    " expected=%" PRIu32 "\n",
                    first_bad, first_actual, first_expected);
    }
    Serial.printf("RESULT kind=raw sample=%" PRIu32 " bytes=%" PRIu32
                  " us=%" PRId64 " mbps=%.6f bad=%" PRIu32 "\n",
                  sample, total, elapsed, total * 8.0 / elapsed, bad);
    CHECK_EQ(total, transfer_size);
    CHECK_EQ(bad, 0u);
    Progress();
    delay(50);
  }
  server_thread.join();
  vSemaphoreDelete(raw_start);
}

// Reads one command without changing board state until all parameters validate.
bool ReadConfig() {
  char command[192];
  size_t length = 0;
  while (true) {
    if (Serial.available() > 0) {
      const int value = Serial.read();
      if (value == '\n') {
        command[length] = 0;
        break;
      }
      if (value != '\r' && length + 1 < sizeof(command))
        command[length++] = value;
    }
    delay(1);
  }
  if (strcmp(command, "reset") == 0) ESP.restart();
  const int fields =
      sscanf(command,
             "run %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
             " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
             " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32,
             &baud, &rx_buffer_size, &tx_buffer_size, &window_log, &mode,
             &transfer_size, &repeats, &application_priority,
             &rx_fifo_threshold, &rx_timeout_symbols, &chunk_size, &split_irq,
             &fault_us, &sender_priority);
  if (fields < 8 || baud == 0 || rx_buffer_size < 129 || window_log > 10 ||
      mode > 3 || transfer_size == 0 || repeats == 0 ||
      application_priority >= configMAX_PRIORITIES - 1 ||
      rx_fifo_threshold == 0 || rx_fifo_threshold > 127 ||
      rx_timeout_symbols > 101 || chunk_size == 0 || chunk_size > kMaxChunk ||
      chunk_size % 256 != 0 || split_irq > 1 ||
      sender_priority >= configMAX_PRIORITIES - 1 ||
      (sender_priority != 0 && mode != 0) ||
      (fault_us != 0 && (mode != 0 || transfer_size < 65536))) {
    Serial.println("ERROR config=1");
    return false;
  }
  return true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("READY");
  if (!ReadConfig()) return;
  for (size_t i = 0; i < sizeof(pattern); ++i)
    pattern[i] = (i * 73 + 19) & 0xff;
  ConfigureUarts();
  SettleUarts();
  if (application_priority != 0)
    vTaskPrioritySet(nullptr, application_priority);
  Progress();
  measuring = true;
  CHECK_EQ(xTaskCreate(Monitor, "bench-monitor", 2048, nullptr,
                       configMAX_PRIORITIES - 1, nullptr),
           pdPASS);
  if (mode == 0)
    LinkTests();
  else
    RawTests();
  Serial.printf("ERRORS observed=%d uart1=%" PRIu32 " uart2=%" PRIu32 "\n",
                mode != 0, uart_errors[0].load(), uart_errors[1].load());
  PrintErrorTypes("ERROR_TYPES");
  Serial.printf("FAULT requested_us=%" PRIu32 " fired=%d restored=%d\n",
                fault_us, fault_applied.load(), fault_restored.load());
  Serial1.end();
  Serial2.end();
  measuring = false;
  Serial.println("DONE");
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == 'r') {
    Serial.flush();
    ESP.restart();
  }
  delay(10);
}
