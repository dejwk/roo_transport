#ifdef ROO_TESTING

// This section is intended for testing the example on Linux. You can disregard
// it when analyzing the example itself - just scroll down to the #endif.

#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"

struct Emulator {
  FakeUartCable cable;

  Emulator() {
    auto& board = FakeEsp32();
    board.attachUartDevice(cable.end_a(), 27, 14);
    board.attachUartDevice(cable.end_b(), 25, 26);
  }
} emulator;

#endif

// This example measures the throughput and latency of a reliable serial link.
// It can run in loopback mode on one ESP32, or on two separate ESP32s.
//
// In loopback mode, cross-connect the two UARTs with jumper wires:
// kPinServerTx to kPinClientRx, and kPinServerRx to kPinClientTx.

#include <algorithm>
#include <cstdio>
#include <memory>

#include "driver/uart.h"
#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_logging.h"
#include "roo_threads.h"
#include "roo_time.h"
#include "roo_transport/link/esp_idf/reliable_uart.h"

using namespace roo_transport;

namespace {

constexpr int kPinServerTx = 27;
constexpr int kPinServerRx = 14;
constexpr int kPinClientTx = 25;
constexpr int kPinClientRx = 26;
constexpr uint32_t kBaudRate = 5000000;

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

esp_idf::ReliableUartLinkTransport serial1(UART_NUM_1, "serial1");
esp_idf::ReliableUartLinkTransport serial2(UART_NUM_2, "serial2");
roo::thread server_thread;

void ConfigureUart(uart_port_t port, int rx_pin, int tx_pin) {
  uart_config_t config = {};
  config.baud_rate = kBaudRate;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;

  ESP_ERROR_CHECK(uart_param_config(port, &config));
  ESP_ERROR_CHECK(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(port, 4096, 4096, 0, nullptr, 0));
}

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

void server() {
  std::printf("Server connecting...\n");
  ConfigureUart(UART_NUM_1, kPinServerRx, kPinServerTx);
  serial1.begin();
  Link link = serial1.connectOrDie();
  std::printf("Server connected.\n");

  std::unique_ptr<roo::byte[]> data(new roo::byte[256]);
  for (int i = 0; i < 256; ++i) {
    data[i] = static_cast<roo::byte>(i);
  }

  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  while (true) {
    uint32_t len = in.readVarU64();
    while (len > 256) {
      out.writeByteArray(data.get(), 256);
      len -= 256;
    }
    out.writeByteArray(data.get(), len);
    out.flush();
    roo::this_thread::yield();
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

void LatencyTest(roo_io::InputStreamReader& in,
                 roo_io::OutputStreamWriter& out) {
  std::printf("Starting latency test...\n");
  constexpr size_t kNumSamples = 1000;
  float rtt[kNumSamples];
  for (size_t i = 0; i < kNumSamples; ++i) {
    roo_time::Uptime start = roo_time::Uptime::Now();
    out.writeVarU64(1);
    out.flush();
    in.readU8();
    roo_time::Uptime end = roo_time::Uptime::Now();
    rtt[i] = (end - start).inMillisFloat();
  }
  std::sort(rtt, rtt + kNumSamples);
  std::printf(
      "Round-trip times [ms] (min, p50, p90, p99, max): "
      "%f, %f, %f, %f, %f\n",
      rtt[0], rtt[kNumSamples / 2], rtt[(kNumSamples * 9) / 10],
      rtt[(kNumSamples * 99) / 100], rtt[kNumSamples - 1]);
  std::printf("Latency test completed.\n");
}

void ThroughputTest(roo_io::InputStreamReader& in,
                    roo_io::OutputStreamWriter& out) {
  std::printf("Starting throughput test...\n");
  std::unique_ptr<roo::byte[]> buf(new roo::byte[256]);

  constexpr uint32_t kMessageSize = 64 * 1024;
  roo_time::Uptime start = roo_time::Uptime::Now();
  out.writeVarU64(kMessageSize);
  out.flush();
  size_t remaining = kMessageSize;
  while (remaining > 256) {
    remaining -= in.readByteArray(buf.get(), 256);
  }
  remaining -= in.readByteArray(buf.get(), remaining);
  roo_time::Uptime end = roo_time::Uptime::Now();
  float time_s = (end - start).inSecondsFloat();
  float throughput_mbps = (kMessageSize / time_s) / (1000.0f * 1000.0f / 8.0f);
  std::printf("Throughput for message size of %u KB: %f Mbps\n",
              static_cast<unsigned>(kMessageSize / 1024), throughput_mbps);
  std::printf("Throughput test completed.\n");
}

void client() {
  std::printf("Client connecting...\n");
  ConfigureUart(UART_NUM_2, kPinClientRx, kPinClientTx);
  serial2.begin();
  Link link = serial2.connectOrDie();
  CHECK_EQ(link.status(), LinkStatus::kConnected);
  std::printf("Client connected.\n");

  roo_io::InputStreamReader in(link.in());
  roo_io::OutputStreamWriter out(link.out());
  while (true) {
    LatencyTest(in, out);
    ThroughputTest(in, out);
    CHECK_EQ(in.status(), roo_io::kOk) << "Input error: " << in.status();
    CHECK_EQ(out.status(), roo_io::kOk) << "Output error: " << out.status();
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

#if MODE == MODE_LOOPBACK

void StartServer() {
  roo::thread::attributes attrs;
  attrs.set_name("server");
  attrs.set_priority(1);
  attrs.set_stack_size(8192);
  server_thread = roo::thread(attrs, &server);
}

#endif  // MODE == MODE_LOOPBACK

}  // namespace

extern "C" void app_main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#if MODE == MODE_CLIENT
  client();
#elif MODE == MODE_SERVER
  server();
#else
  StartServer();
  client();
#endif
}
