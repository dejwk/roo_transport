#include <Arduino.h>
#include <FreeRTOS.h>
#include <hardware/clocks.h>
#include <hardware/uart.h>
#include <hardware/watchdog.h>
#include <semphr.h>
#include <task.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include "raw_dma_stream.h"
#include "roo_collections/hash.h"
#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_transport/link/arduino/reliable_serial.h"
#include "roo_transport/link/internal/out_buffer.h"
#include "roo_transport/link/pico/reliable_uart_dma.h"

namespace {
uint32_t baud = 8000000;
uint32_t fifo_size = 1024;
uint32_t window_log = 4;
uint32_t mode = 10;
uint32_t transfer_size = 262144;
uint32_t repeats = 3;
uint32_t sys_peri = 0;
uint32_t application_priority = 0;
uint32_t fault_us = 0;
uint32_t uart0_init_core = 0;
std::atomic<uint32_t> received_progress{0};
std::atomic<bool> measuring{true};
std::atomic<uint32_t> last_request{0};
std::atomic<uint32_t> enqueued{0};
uint8_t pattern[256];
roo_transport::LinkStreamTransport* server_transport;
roo_transport::LinkStreamTransport* client_transport;
alignas(8192) uint8_t dma_ring1[8192];
alignas(8192) uint8_t dma_ring2[8192];
BenchDmaStream* dma_serial1;
BenchDmaStream* dma_serial2;
roo_transport::pico::ReliableUartDma* library_server;
roo_transport::pico::ReliableUartDma* library_client;
roo::thread server_thread;
SemaphoreHandle_t raw_start;
std::atomic<bool> raw_done{false};

// Configures the crossed UARTs and reports actual, rather than requested,
// clocks.
void ConfigureUarts() {
  if (sys_peri != 0) {
    const uint32_t hz = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, hz,
                    hz);
  }
  uint32_t actual_baud;
  if (mode == 7 || mode == 10) {
    gpio_set_function(12, GPIO_FUNC_UART);
    gpio_set_function(13, GPIO_FUNC_UART);
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    actual_baud = uart_init(uart0, baud);
    uart_init(uart1, baud);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    if (mode != 10) {
      dma_serial1 = new BenchDmaStream(uart0, dma_ring1);
      dma_serial2 = new BenchDmaStream(uart1, dma_ring2);
    }
    fifo_size = 8192;
  } else {
    Serial1.setPinout(12, 13);
    Serial2.setPinout(4, 5);
    Serial1.setFIFOSize(fifo_size);
    Serial2.setFIFOSize(fifo_size);
    if (mode != 5) Serial1.begin(baud, SERIAL_8N1);
    Serial2.begin(baud, SERIAL_8N1);
    actual_baud = Serial2.getActualBaud();
  }
  Serial.printf(
      "CONFIG baud=%lu actual=%d cpu=%lu peri=%lu fifo=%lu window=%lu mode=%lu "
      "bytes=%lu repeats=%lu cores=%d tick=%d app_prio=%lu fault_us=%lu\n",
      baud, actual_baud, clock_get_hz(clk_sys), clock_get_hz(clk_peri),
      fifo_size, window_log, mode, transfer_size, repeats,
      configNUMBER_OF_CORES, configTICK_RATE_HZ, application_priority,
      fault_us);
}

// Starts the selected Arduino or native DMA endpoint.
void BeginEndpoint(bool server) {
  roo_transport::LinkStreamTransport* endpoint =
      server ? server_transport : client_transport;
  if (mode == 10) {
    (server ? library_server : library_client)->begin();
  } else if (server) {
    static_cast<roo_transport::ReliableSerial1*>(endpoint)->begin();
  } else {
    static_cast<roo_transport::ReliableSerial2*>(endpoint)->begin();
  }
}

// Establishes a stream using the selected backend.
roo_transport::LinkStream ConnectEndpoint(bool server) {
  if (mode == 10)
    return roo_transport::LinkStream(
        (server ? library_server : library_client)->transport().connectOrDie());
  return (server ? server_transport : client_transport)->connectOrDie();
}

// Borrows packet counters from the selected backend.
roo_transport::LinkTransport::StatsMonitor EndpointStats(bool server) {
  if (mode == 10)
    return roo_transport::LinkTransport::StatsMonitor(
        (server ? library_server : library_client)->transport());
  return (server ? server_transport : client_transport)->statsMonitor();
}

// Serves the fixed latency and throughput request counts, then stops.
void Server() {
  if (mode == 5) {
    Serial1.begin(baud, SERIAL_8N1);
    uart0_init_core = get_core_num();
  }
  Serial.printf("OWNER uart0_init_core=%lu server_task_core=%u\n",
                uart0_init_core, get_core_num());
  BeginEndpoint(true);
  roo_transport::LinkStream link = ConnectEndpoint(true);
  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  for (uint32_t request = 0; request < 100 + repeats; ++request) {
    uint32_t count = in.readVarU64();
    last_request.store(count);
    if (count > 1) Serial.printf("REQUEST len=%lu\n", count);
    while (count != 0) {
      const size_t length = std::min<uint32_t>(count, sizeof(pattern));
      out.writeByteArray(reinterpret_cast<roo::byte*>(pattern), length);
      enqueued.fetch_add(length);
      count -= length;
    }
    out.flush();
  }
  out.close();
}

// Sends one validated-pattern response per semaphore request without framing.
void RawServer() {
  for (uint32_t sample = 0; sample < repeats; ++sample) {
    xSemaphoreTake(raw_start, portMAX_DELAY);
    uint32_t remaining = transfer_size;
    while (remaining != 0) {
      const size_t length = std::min<uint32_t>(remaining, sizeof(pattern));
      if (mode == 7)
        dma_serial1->write(pattern, length);
      else
        Serial1.write(pattern, length);
      remaining -= length;
    }
    if (mode == 7)
      dma_serial1->flush();
    else
      Serial1.flush();
    raw_done.store(true, std::memory_order_release);
  }
}

// Starts the producer at the configured application priority.
void StartServer(void (*fn)()) {
  roo::thread::attributes attrs;
  attrs.set_name("bench-server");
  attrs.set_priority(application_priority == 0 ? 1 : application_priority);
  attrs.set_stack_size(8192);
  server_thread = roo::thread(attrs, fn);
}

// Verifies wire-rate transfers independently of the reliable protocol.
void RawTests() {
  raw_start = xSemaphoreCreateBinary();
  StartServer(RawServer);
  uint8_t received[256];
  for (uint32_t sample = 0; sample < repeats; ++sample) {
    raw_done.store(false, std::memory_order_release);
    Serial1.overflow();
    Serial2.overflow();
    size_t total = 0;
    size_t bad = 0;
    uint64_t last_data = time_us_64();
    const uint64_t start = last_data;
    xSemaphoreGive(raw_start);
    while (true) {
      const int available =
          mode == 7 ? dma_serial2->available() : Serial2.available();
      if (available > 0) {
        const size_t count =
            mode == 7
                ? dma_serial2->readAvailable(
                      received, std::min<size_t>(available, sizeof(received)))
                : Serial2.readBytes(
                      received, std::min<size_t>(available, sizeof(received)));
        for (size_t i = 0; i < count; ++i) {
          if (received[i] != pattern[(total + i) % sizeof(pattern)]) ++bad;
        }
        total += count;
        received_progress.fetch_add(count, std::memory_order_relaxed);
        last_data = time_us_64();
        watchdog_update();
      } else if (raw_done.load(std::memory_order_acquire) &&
                 time_us_64() - last_data > 2000) {
        break;
      } else {
        taskYIELD();
      }
    }
    const uint64_t elapsed = last_data - start;
    const bool overflow =
        mode == 7 ? dma_serial2->overruns != 0 : Serial2.overflow();
    Serial.printf(
        "RESULT kind=raw sample=%lu bytes=%u us=%llu mbps=%.6f bad=%u "
        "overflow=%d\n",
        sample, total, elapsed, total * 8.0 / elapsed, bad, overflow);
    CHECK_EQ(bad, 0u);
    CHECK_EQ(total, transfer_size);
    watchdog_update();
    delay(50);
  }
}

void InjectFault(void*);

// Verifies latency, ordered payload delivery, worker stop and buffer lifetimes.
void LinkTests() {
  const auto window = static_cast<roo_transport::LinkBufferSize>(window_log);
  if (mode == 10) {
    library_server = new roo_transport::pico::ReliableUartDma(uart0, dma_ring1,
                                                              window, window);
    library_client = new roo_transport::pico::ReliableUartDma(uart1, dma_ring2,
                                                              window, window);
  } else {
    server_transport = new roo_transport::ReliableSerial1(window, window);
    client_transport = new roo_transport::ReliableSerial2(window, window);
  }
  StartServer(Server);
  BeginEndpoint(false);
  {
    roo_transport::LinkStream link = ConnectEndpoint(false);
    roo_io::InputStreamReader in(link.in());
    roo_io::OutputStreamWriter out(link.out());
    Serial.println("CONNECTED");
    uint32_t rtt[100];
    for (size_t i = 0; i < 100; ++i) {
      const uint64_t start = time_us_64();
      out.writeVarU64(1);
      out.flush();
      CHECK_EQ(in.readU8(), pattern[0]);
      rtt[i] = time_us_64() - start;
      watchdog_update();
    }
    std::sort(rtt, rtt + 100);
    Serial.printf("LATENCY min=%lu p50=%lu p90=%lu p99=%lu max=%lu\n", rtt[0],
                  rtt[50], rtt[90], rtt[99], rtt[99]);
    roo::byte received[256];
    if (fault_us != 0)
      xTaskCreate(InjectFault, "fault", 1024, nullptr, configMAX_PRIORITIES - 1,
                  nullptr);
    auto stats = EndpointStats(true);
    for (uint32_t sample = 0; sample < repeats; ++sample) {
      delay(25);
      const uint32_t sent_before = stats.packets_sent();
      const uint32_t delivered_before = stats.packets_delivered();
      Serial1.overflow();
      Serial2.overflow();
      uint32_t remaining = transfer_size;
      size_t bad = 0;
      const uint64_t start = time_us_64();
      out.writeVarU64(transfer_size);
      out.flush();
      while (remaining != 0) {
        const size_t count = in.readByteArray(
            received, std::min<uint32_t>(remaining, sizeof(received)));
        CHECK_GT(count, 0u);
        for (size_t i = 0; i < count; ++i) {
          if (static_cast<uint8_t>(received[i]) !=
              pattern[(transfer_size - remaining + i) % sizeof(pattern)])
            ++bad;
        }
        remaining -= count;
        received_progress.fetch_add(count, std::memory_order_relaxed);
        watchdog_update();
      }
      const uint64_t elapsed = time_us_64() - start;
      delay(25);
      Serial.printf(
          "RESULT kind=link sample=%lu bytes=%lu us=%llu mbps=%.6f bad=%u "
          "sent=%lu delivered=%lu overflow_server=%d overflow_client=%d\n",
          sample, transfer_size, elapsed, transfer_size * 8.0 / elapsed, bad,
          stats.packets_sent() - sent_before,
          stats.packets_delivered() - delivered_before, Serial1.overflow(),
          Serial2.overflow());
      CHECK_EQ(bad, 0u);
      CHECK_EQ(in.status(), roo_io::kOk);
      CHECK_EQ(out.status(), roo_io::kOk);
      watchdog_update();
    }
    CHECK_EQ(in.readByteArray(received, 1), 0u);
    CHECK_EQ(in.status(), roo_io::kEndOfStream);
    out.close();
    server_thread.join();
  }
}
// Allows stalled firmware to reset without printing inside measured transfers.
void Monitor(void*) {
  const uint64_t start = time_us_64();
  while (measuring.load()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (time_us_64() - start < 10000000) watchdog_update();
  }
  vTaskDelete(nullptr);
}

// Forces a receive break without driving either crossed wire electrically.
void InjectFault(void*) {
  while (received_progress.load() < 32768) vTaskDelay(1);
  gpio_set_inover(5, GPIO_OVERRIDE_LOW);
  vTaskDelay(pdMS_TO_TICKS((fault_us + 999) / 1000));
  gpio_set_inover(5, GPIO_OVERRIDE_NORMAL);
  vTaskDelete(nullptr);
}

// Measures the cost of consuming bytes already buffered by Arduino's UART IRQ.
void DriverCost() {
  uint8_t buffer[256];
  uint64_t total = 0;
  size_t bad = 0;
  for (uint32_t i = 0; i < 100; ++i) {
    Serial1.write(pattern, sizeof(pattern));
    Serial1.flush();
    while (Serial2.available() < 256) taskYIELD();
    const uint64_t start = time_us_64();
    const size_t count = Serial2.readBytes(buffer, sizeof(buffer));
    total += time_us_64() - start;
    CHECK_EQ(count, sizeof(buffer));
    for (size_t j = 0; j < count; ++j)
      if (buffer[j] != pattern[j]) ++bad;
    watchdog_update();
  }
  Serial.printf("RESULT kind=driver bytes=25600 us=%llu mbps=%.6f bad=%u\n",
                total, 25600 * 8.0 / total, bad);
  CHECK_EQ(bad, 0u);
}
// Isolates per-packet costs from UART and scheduling dependencies.
void CpuTests() {
  constexpr uint32_t count = 1000;
  uint64_t start = time_us_64();
  roo_transport::internal::OutBuffer packet;
  for (uint32_t i = 0; i < count; ++i) {
    packet.init(roo_transport::internal::SeqNum(i), false);
    packet.markSent(roo_time::Uptime::Now());
    watchdog_update();
  }
  Serial.printf("RESULT kind=cpu op=mark_sent iterations=%lu us=%llu\n", count,
                time_us_64() - start);
  volatile uint32_t hash = 0;
  start = time_us_64();
  for (uint32_t i = 0; i < count; ++i)
    hash = roo_collections::murmur3_32(pattern, 248, i);
  Serial.printf("RESULT kind=cpu op=hash iterations=%lu us=%llu value=%lu\n",
                count, time_us_64() - start, hash);
  roo::mutex mutex;
  start = time_us_64();
  for (uint32_t i = 0; i < count; ++i) {
    mutex.lock();
    mutex.unlock();
  }
  Serial.printf("RESULT kind=cpu op=mutex iterations=%lu us=%llu\n", count,
                time_us_64() - start);
  start = time_us_64();
  for (uint32_t i = 0; i < count; ++i) roo::this_thread::yield();
  Serial.printf("RESULT kind=cpu op=yield iterations=%lu us=%llu\n", count,
                time_us_64() - start);
  volatile int retry = 1;
  volatile double result;
  start = time_us_64();
  for (uint32_t i = 0; i < count; ++i) result = pow(1.33, retry);
  Serial.printf("RESULT kind=cpu op=pow iterations=%lu us=%llu\n", count,
                time_us_64() - start);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.printf("READY reset=%d\n", watchdog_caused_reboot());
  char config[160];
  size_t pos = 0;
  while (true) {
    if (Serial.available() > 0) {
      const int byte = Serial.read();
      if (byte == '\n') {
        config[pos] = 0;
        break;
      }
      if (pos + 1 < sizeof(config)) config[pos++] = byte;
    }
    delay(1);
  }
  if (strcmp(config, "reset") == 0) rp2040.reboot();
  if (sscanf(config, "run %lu %lu %lu %lu %lu %lu %lu %lu %lu", &baud,
             &fifo_size, &window_log, &mode, &transfer_size, &repeats,
             &sys_peri, &application_priority, &fault_us) < 6) {
    Serial.println("ERROR config");
    return;
  }
  for (size_t i = 0; i < sizeof(pattern); ++i)
    pattern[i] = (i * 73 + 19) & 0xff;
  ConfigureUarts();
  if (application_priority != 0)
    vTaskPrioritySet(nullptr, application_priority);
  watchdog_enable(8000, true);
  xTaskCreate(Monitor, "monitor", 2048, nullptr, configMAX_PRIORITIES - 1,
              nullptr);
  if (mode == 1 || mode == 7)
    RawTests();
  else if (mode == 2)
    DriverCost();
  else if (mode == 9)
    CpuTests();
  else
    LinkTests();
  if (mode == 10)
    Serial.printf(
        "DMA overruns1=%lu overruns2=%lu errors1=%lu errors2=%lu\n",
        library_server->receiveOverruns(), library_client->receiveOverruns(),
        library_server->receiveErrors(), library_client->receiveErrors());
  else if (mode == 7)
    Serial.printf("DMA overruns1=%lu overruns2=%lu\n", dma_serial1->overruns,
                  dma_serial2->overruns);
  if (mode == 10) {
    library_server->end();
    library_client->end();
    delete library_server;
    delete library_client;
    for (int attempt = 0; attempt < 5; ++attempt) {
      roo_transport::pico::ReliableUartDma idle(uart0, dma_ring1);
      idle.begin();
      idle.end();
    }
    Serial.println("LIFECYCLE idle_stop_and_reuse=PASS");
  }
  if (mode == 1 || mode == 7) server_thread.join();
  measuring.store(false);
  watchdog_disable();
  Serial.println("DONE");
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == 'r') {
    Serial.flush();
    rp2040.reboot();
  }
  delay(10);
}
