// This example demonstrates how to use the packet transport.
//
// The transport guarantees integrity (i.e. no corrupted packets), but does not
// guarantee delivery (i.e. packet may get lost in transit) or ordering (i.e.
// occassionally packets may arrive out of order). This is often sufficient for
// simple sensor data transfer, where occasional packet loss is acceptable.

// Important: to run this example, you need to connect the 'server' TX pin to
// the 'client' RX pin.

// Feel free to disconnect the wires during the test and see what happens. You
// should notice that the client stops receiving notifications. But the
// notifications that it does receive are always intact.

#include "roo_io.h"
#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/memory_input_iterator.h"
#include "roo_io/memory/memory_output_iterator.h"
#include "roo_io/uart/arduino/serial_input_stream.h"
#include "roo_io/uart/arduino/serial_output_stream.h"
#include "roo_threads.h"
#include "roo_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

using namespace roo_transport;

static const int kPinServerTx = 27;
static const int kPinServerRx = 14;
static const int kPinClientTx = 25;
static const int kPinClientRx = 26;

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

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

void server() {
  // The server simulates a simple temperature sensor which reports temperature
  // (in centi-degrees Celsius) once per second, by sending packets over UART
  // (Serial1).
#ifdef ARDUINO_ARCH_RP2040
  Serial1.setPinout(kPinServerTx, kPinServerRx);
  Serial1.begin(5000000, SERIAL_8N1);
#else
  Serial1.begin(5000000, SERIAL_8N1, kPinServerRx, kPinServerTx);
#endif

  roo_io::ArduinoSerialOutputStream serial1_out(Serial1);
  PacketSenderOverStream sender(serial1_out);

  size_t num_reading = 0;
  while (true) {
    int32_t temperature =
        2500 + (num_reading % 1000);  // sawtooth 25.00 C -> 35.00 C

    // Prepare the packet.
    roo::byte buf[8];
    roo_io::MemoryOutputIterator itr(buf, buf + 8);
    roo_io::WriteBeU32(itr, num_reading);
    roo_io::WriteBeS32(itr, temperature);

    CHECK_EQ(itr.status(), roo_io::kOk);

    // Send the packet.
    sender.send(buf, itr.ptr() - buf);

    Serial.printf("Server sent reading #%d: %f\n", num_reading,
                  ((float)temperature) / 100.0f);
    num_reading++;
    delay(1000);
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

void processPacket(const roo::byte* buf, size_t len) {
  roo_io::MemoryIterator itr(buf, buf + len);
  uint32_t reading_num = roo_io::ReadBeU32(itr);
  int32_t temperature = roo_io::ReadBeS32(itr);
  if (itr.status() != roo_io::kOk) {
    Serial.printf("Client: error reading packet: %d %d\n", itr.status(), len);
    return;
  }
  Serial.printf("Client received reading #%d: %f\n", reading_num,
                ((float)temperature) / 100.0f);
}

void client() {
  // Handle messages in a loop.
#ifdef ARDUINO_ARCH_RP2040
  Serial2.setPinout(kPinClientTx, kPinClientRx);
  Serial2.begin(5000000, SERIAL_8N1);
#else
  Serial2.begin(5000000, SERIAL_8N1, kPinClientRx, kPinClientTx);
#endif
  roo_io::ArduinoSerialInputStream serial2_in(Serial2);
  PacketReceiverOverStream receiver(serial2_in);
  while (true) {
    size_t num_received = receiver.receive(processPacket);
    CHECK_GT(num_received, 0);
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
  attrs.set_stack_size(4096);
  server_thread = roo::thread(attrs, &server);
}

void setup() {
  Serial.begin(115200);
  startServer();
  client();
}

#endif

void loop() {}
