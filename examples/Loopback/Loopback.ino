// This example demonstrates a reliable serial link in loopback mode, where
// the server and client are connected to each other using two different UARTs
// on the same device. The server sends a sequence of messages to the client,
// which receives and prints them to the standard output.
//
// Important: to run this example, you need to connect the TX and RX pins of the
// two UARTs to each other using jumper wires. The pin numbers are defined by
// the constants kPinServerTx, kPinServerRx, kPinClientTx, and
// kPinClientRx below. Make sure to cross the wires, i.e. to connect
// kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.

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

roo::thread server_thread;

// ReliableSerial1 and ReliableSerial2 are (near) drop-in replacements for
// Serial1 and Serial2, that implement a reliable transport on top the
// underlying UART connection.
ReliableSerial1 serial1;
ReliableSerial2 serial2;

void server() {
  LOG(INFO) << "Server connecting...";
  serial1.begin(5000000, SERIAL_8N1, kPinServerRx, kPinServerTx);
  LOG(INFO) << "Server connected.";

  roo_io::OutputStreamWriter serial1_out(serial1.out());
  uint32_t i = 0;
  while (true) {
    serial1_out.writeBeU32(i++);
    serial1_out.writeString("Hello, world!\n");
    yield();
  }
}

// Start the server in a separate task thread, so that it runs concurrently with
// the client (which executes in the main loop).
void startServer() {
  roo::thread::attributes attrs;
  attrs.set_name("server");
  // Use low priority to avoid starving the (receiving) client thread in the
  // loopback mode.
  attrs.set_priority(1);
  server_thread = roo::thread(attrs, &server);
}

void client() {
  LOG(INFO) << "Client connecting...";
  serial2.begin(5000000, SERIAL_8N1, kPinClientRx, kPinClientTx);
  LOG(INFO) << "Client connected.";

  roo_io::InputStreamReader serial2_in(serial2.in());
  while (true) {
    uint32_t idx = serial2_in.readBeU32();
    std::string msg = serial2_in.readString();
    if ((idx % 1000) == 0) {
      // Print one in every 1000 messages to avoid flooding the output.
      LOG(INFO) << "Received " << idx << ": " << msg;
    }
  }
}

void setup() {
  Serial.begin(115200);
  startServer();
  client();
}

void loop() {}
