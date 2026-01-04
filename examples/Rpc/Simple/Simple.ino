#ifdef ROO_TESTING

// This section is intended for testing the example on Linux. You can disregard
// it when analyzing the example itself - just scroll down to the #endif.

#include "roo_io/ringpipe/ringpipe.h"
#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"

class FakeUartEndpoint : public FakeUartDevice {
 public:
  FakeUartEndpoint() : tx_(256), rx_(256) {}

  size_t write(const uint8_t* buf, uint16_t size) override {
    return tx_.writeFully((const roo::byte*)buf, size);
  }

  size_t read(uint8_t* buf, uint16_t size) override {
    return rx_.tryRead((roo::byte*)buf, size);
  }

  size_t availableForRead() override { return rx_.availableForRead(); }

  size_t availableForWrite() override { return tx_.availableForWrite(); }

  roo_io::RingPipe& tx() { return tx_; }
  roo_io::RingPipe& rx() { return rx_; }

 private:
  roo_io::RingPipe tx_;
  roo_io::RingPipe rx_;
};

class UartForwarder {
 public:
  UartForwarder(roo_io::RingPipe& from, roo_io::RingPipe& to,
                FakeUartDevice& recv)
      : from_(from), to_(to), recv_(recv) {}

  void begin() {
    roo::thread::attributes attrs;
    attrs.set_name("uart forwarder");
    forwarder_thread_ = roo::thread(attrs, [this]() {
      roo::byte buffer[256];
      while (true) {
        size_t count = from_.read(buffer, sizeof(buffer));
        if (count == 0) {
          break;
        }
        do {
          size_t written = to_.write(buffer, count);
          recv_.notifyDataAvailable();
          if (written == 0) {
            break;
          }
          count -= written;
        } while (count > 0);
      }
    });
  }

 private:
  roo_io::RingPipe& from_;
  roo_io::RingPipe& to_;
  FakeUartDevice& recv_;
  roo::thread forwarder_thread_;
};

struct Emulator {
  FakeUartEndpoint serial1_;
  FakeUartEndpoint serial2_;
  UartForwarder forwarder_1_to_2_;
  UartForwarder forwarder_2_to_1_;
  Emulator()
      : forwarder_1_to_2_(serial1_.tx(), serial2_.rx(), serial2_),
        forwarder_2_to_1_(serial2_.tx(), serial1_.rx(), serial1_) {
    forwarder_1_to_2_.begin();
    forwarder_2_to_1_.begin();

    auto& board = FakeEsp32();
    board.attachUartDevice(serial1_, 27, 14);
    board.attachUartDevice(serial2_, 25, 26);
  }
} emulator;

#endif

// In this example, we implement a simple but functional RPC system. Our RPC
// server is able to handle multiple RPC functions, and to return RPC errors. If
// the server gets reset during a request, that particular RPC fails, but the
// overall system keeps going, and the client can just retry the request.

// The RPC layer, demonstrated in this example, takes advantage of the
// properties of the SerialLinkTransport (a reliable bidirectional link over
// inherently unreliable UART), overlaid with the Messaging transport, which is
// able to gracefully handle resets / reconnections.

// Important: to run this example in the loopback mode, you need to connect the
// TX and RX pins of the two UARTs to each other using jumper wires. The pin
// numbers are defined by the constants kPinServerTx, kPinServerRx,
// kPinClientTx, and kPinClientRx below. Make sure to cross the wires, i.e. to
// connect kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.

#include "roo_threads.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/reliable_serial.h"
#include "roo_transport/link/link_messaging.h"
#include "roo_transport/rpc/client.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/server.h"

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

// This is the definition of our RPC service, which essentially lists all
// supported RPC functions. In our example, we only use one function.
enum FunctionId {
  kSquareFn = 0,
};

// This constant defines the maximum size of the payload requests and responses.
// The sizes are constrained only by the available RAM (as the requests and the
// responses must fit entirely in RAM during the RPC call). This value
// determines the size of the (singleton) buffers used by the LinkMessaging
// transport layer for the received messages.
//
// In our case, we can keep this value tiny, as our requests and responses are
// just 32-bit integers, i.e. 4 bytes. (The value must account for some extra
// overhead of the RPC message header, but that is up to 24 bytes).
//
// There's no significant downside to making this value larger than minimally
// necessary, other than the extra RAM consumption.
static const int kMaxPayloadSize = 32;

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

Status calcSquare(uint32_t x, uint32_t& result) {
  Serial.printf("Server: Received a request to square %d\n", x);

  // Pretend to be doing something useful.
  delay(1000);

  result = x * x;

  Serial.printf("Server: Completed the request to square %d\n", x);
  return kOk;
}

// This is the dispatch table of all RPC functions registered in the server
// (in our case, just our single function defined above).
//
// We use the UnaryHandler, which is a convenience wrapper that 'promotes' a
// function (Status)(const Request&, Response&) to a proper handler that the
// RPC server knows how to work with. The handler takes care of serialization
// and deserialization, and basic error handling.
FunctionTable rpc_function_table = {
    {kSquareFn, UnaryHandler<uint32_t, uint32_t>(calcSquare)},
};

// Declares the reliable bidirectional server-side transport over UART.
ReliableSerial1 server_serial;

// Declares the messaging layer on top of the reliable transport.
LinkMessaging server_messaging(server_serial, kMaxPayloadSize);

// Declares the RPC server on top of the messaging layer.
RpcServer rpc_server(server_messaging, &rpc_function_table);

// This is our server's entry point.
void server() {
  // Initialize UART.
  Serial1.begin(5000000, SERIAL_8N1, kPinServerRx, kPinServerTx);

  // Initialize the reliable serial transport.
  server_serial.begin();

  // Initialize the RPC server to be ready to receive request messages.
  rpc_server.begin();

  // Starts the singleton thread that receives and dispatches the request
  // messages.
  server_messaging.begin();

  // Just idle - we don't need the loop thread anymore. The requests are handled
  // in the thread created above. In real apps, you can use this thread to run
  // some other logic.
  while (true) {
    delay(1000);
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

// Declares the reliable bidirectional client-side transport over UART.
ReliableSerial2 client_serial;

// Declares the messaging layer on top of the reliable transport.
LinkMessaging client_messaging(client_serial, kMaxPayloadSize);

// Declares the RPC client on top of the messaging layer.
RpcClient rpc_client(client_messaging);

// Declares the stub for our single RPC function. Uses the UnaryStub helper
// which handles serialization/deserialization, basic error handling, and
// synchronous calls (as the underlying RPC layer is inherently asynchronous).
UnaryStub<uint32_t, uint32_t> square_stub(rpc_client, kSquareFn);

void client() {
  // Initialize UART.
  Serial2.begin(5000000, SERIAL_8N1, kPinClientRx, kPinClientTx);

  // Initialize the reliable serial transport.
  client_serial.begin();

  // Initialize the RPC client to be ready to receive response messages.
  rpc_client.begin();

  // Start the singleton thread that receives and dispatches the response
  // messages.
  client_messaging.begin();

  // Make requests in a loop.
  while (true) {
    for (uint32_t request = 1; request < 1000; request++) {
      uint32_t response;
      roo_transport::Status result = square_stub.call(request, response);
      if (result != roo_transport::kOk) {
        Serial.printf("RPC failed: %d\n", result);
      } else {
        Serial.printf("Success: %d * %d = %d\n", request, request, response);
      }
      Serial.println();
      delay(1000);
    }
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

// Never called; all the work is done in setup() and new threads.
void loop() {}
