// This example is a simple benchmark that measures throughput and latency of
// the reliable serial. It can be configured to run in a loopback mode (using a
// single microcontroller), or to run on two separate microcontrollers.
//
// Important: to run this example in the loopback mode, you need to connect the
// TX and RX pins of the two UARTs to each other using jumper wires. The pin
// numbers are defined by the constants kPinServerTx, kPinServerRx,
// kPinClientTx, and kPinClientRx below. Make sure to cross the wires, i.e. to
// connect kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.
//
// In case of two separate microcontrollers, connect the TX pin of the server
// to the RX pin of the client, and the RX pin of the server to the TX pin of
// the client.

//
// Results for two ESP32s connected using short wires using 5000000 baud
// in the most commonly used 8N1 mode (10 bits per byte):
//
// Throughput for message size of 512 KB: 3.393532 Mbps
// Round-trip times [ms] (min, p50, p90, p99, max):
// 0.468000, 0.579000, 0.602000, 0.669000, 0.762000
//
// In other words, we're reaching 85% of the theoretical maximum throughput of
// the ESP32 UART, and we see round-trip latency median of only 0.58 ms.
//

// Feel free to disconnect the wires during the test and see what happens. You
// should notice that the benchmark stalls, and then seamlessly resumes once the
// connection is restored.

#include "roo_io.h"
#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/reliable_serial.h"

using namespace roo_transport;

static const int kPinServerTx = 27;
static const int kPinServerRx = 14;
static const int kPinClientTx = 25;
static const int kPinClientRx = 26;

static const uint32_t baud_rate = 5000000;

// Build for a single microcontroller in loopback mode.
#define MODE_LOOPBACK 0

// Build for the server microcontroller.
#define MODE_SERVER 1

// Build for the client microcontroller.
#define MODE_CLIENT 2

// Select the desired mode.
#define MODE LOOPBACK
// #define MODE MODE_SERVER
// #define MODE MODE_CLIENT

roo::thread server_thread;

ReliableSerial1 serial1("server");
ReliableSerial2 serial2("client");

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

void server() {
  // The server runs in a loop that accepts requests consisting of a single
  // integer, and responds by sending back the number of (arbitrary) bytes
  // indicated by that integer.
  // The argument is variable-length-encoded, so that small requests (e.g. 1
  // byte) incur minimal overhead (also 1 byte).
  Serial.println("Server connecting...");
  serial1.begin(baud_rate, SERIAL_8N1, kPinServerRx, kPinServerTx);
  Serial.println("Server connected.");

  // Generate data that we will serve.
  std::unique_ptr<roo::byte[]> data(new roo::byte[256]);
  for (int i = 0; i < 256; i++) {
    data[i] = (roo::byte)i;
  }

  roo_io::InputStreamReader in(serial1.in());
  roo_io::OutputStreamWriter out(serial1.out());
  uint32_t i = 0;
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

void latencyTest(roo_io::InputStreamReader& in,
                 roo_io::OutputStreamWriter& out) {
  Serial.println("Starting latency test...");
  const size_t kNumSamples = 1000;
  float rtt[kNumSamples];
  for (size_t i = 0; i < kNumSamples; i++) {
    roo_time::Uptime start = roo_time::Uptime::Now();
    out.writeVarU64(1);
    out.flush();
    in.readU8();
    roo_time::Uptime end = roo_time::Uptime::Now();
    rtt[i] = (end - start).inMillisFloat();
  }
  std::sort(rtt, rtt + kNumSamples);
  Serial.printf(
      "Round-trip times [ms]: (min, p50, p90, p99, max) : %f, %f, %f, %f, %f\n",
      rtt[0], rtt[kNumSamples / 2], rtt[(kNumSamples * 9) / 10],
      rtt[(kNumSamples * 99) / 100], rtt[kNumSamples - 1]);
  Serial.println("Latency test completed.");
}

void throughputTest(roo_io::InputStreamReader& in,
                    roo_io::OutputStreamWriter& out) {
  Serial.println("Starting throughput test...");
  // The throughput test sends a request for N bytes, reads the response, and
  // measures the time it took to receive the response. It then computes and
  // prints the throughput.
  std::unique_ptr<roo::byte[]> buf(new roo::byte[256]);

  const uint32_t kMessageSize = 512 * 1024;
  uint32_t total_time_us = 0;
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
  Serial.printf("Throughput for message size of %d KB: %f Mbps\n", kMessageSize / 1024,
         throughput_mbps);
  Serial.println("Throughput test completed.");
}

void client() {
  Serial.println("Client connecting...");
  serial2.begin(baud_rate, SERIAL_8N1, kPinClientRx, kPinClientTx);
  CHECK_EQ(serial2.status(), LinkStatus::kConnected);
  Serial.println("Client connected.");

  roo_io::InputStreamReader in(serial2.in());
  roo_io::OutputStreamWriter out(serial2.out());

  while (true) {
    latencyTest(in, out);
    throughputTest(in, out);
    CHECK_EQ(in.status(), roo_io::kOk) << "Input error: " << in.status();
    CHECK_EQ(out.status(), roo_io::kOk) << "Output error: " << out.status();
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
  // Use low priority to avoid starving the (receiving) client thread in the
  // loopback mode.
  attrs.set_priority(1);
  attrs.set_stack_size(8192);
  server_thread = roo::thread(attrs, &server);
}

void setup() {
  Serial.begin(115200);
  startServer();
  client();
}

#endif

void loop() {}
