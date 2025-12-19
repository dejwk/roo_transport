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

// Note: see the Simple.ino for an intro example.
//
// This example demonstrated how to implement an RPC server that can handle
// multiple concurrent requests. In 'real' RPC servers, it is common to have
// multiple threads handling incoming requests. On microcontrollers, this
// doesn't make that much sense, because we don't have as many CPU cores, and
// threads take up precious RAM. However, if the RPC functions involve waiting
// (e.g., for I/O or external event notifications), we can take advantage of
// asynchrony and callbacks to allow multiple requests to be processed
// concurrently, even though we still use only one receiver thread. The way to
// do this is to pass the RPC response callback, received in the RPC request, to
// the routine that will call it when the notification arrives, and then
// immediately return control to the RPC server.
//
// To demonstrate this, in this example we're using a helper scheduler class on
// the server side, which is able to schedule tasks to be executed after a
// specified delay. The RPC server acts as an 'alarm clock' service: it receives
// requests to set alarms after a given delay, and then notifies the client (by
// returning from the RPC) when the alarm goes off. The server can handle
// thousands of concurrent requests this way, although it imposes some (high)
// limit on maximum concurrent requests, to protect itself from memory
// exhaustion. When that limit is reached, new requests become blocking.
// (We could also choose to return an error immediately, and allow the caller to
// handle retries and backoff).
//
// On the client side, we use asynchronous RPCs as well, so that the client can
// issue multiple requests without waiting for each to complete. (The responses
// are delivered via callbacks in the RPC client's receiver thread).
//
// Important: to run this example in the loopback mode, you need to connect the
// TX and RX pins of the two UARTs to each other using jumper wires. The pin
// numbers are defined by the constants kPinServerTx, kPinServerRx,
// kPinClientTx, and kPinClientRx below. Make sure to cross the wires, i.e. to
// connect kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.

#include "roo_scheduler.h"
#include "roo_threads.h"
#include "roo_threads/semaphore.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/serial_link_transport.h"
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

// This is the definition of our RPC 'timer' service.
enum FunctionId {
  kTimerFn = 0,
};

using RequestId = uint32_t;
using DelayMs = uint32_t;

using TimerArg = std::pair<RequestId, DelayMs>;

static const int kMaxPayloadSize = 128;

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

// Helper to implement the 'timer' logic.
roo_scheduler::Scheduler scheduler;

constexpr uint32_t kMaxConcurrentRequests = 5000;

// We use a counting semaphore to keep track of how many requests we're
// currently handling, and to make the server block upon exhaustion.
roo::counting_semaphore<kMaxConcurrentRequests> pending_requests(
    kMaxConcurrentRequests);

void scheduleTimer(const TimerArg& arg,
                   std::function<void(Status status, Void& result)> callback) {
  LOG(INFO) << "Server: received a request #" << arg.first << " to delay for "
            << arg.second << " ms";
  pending_requests.acquire();
  scheduler.scheduleAfter(roo_time::Millis(arg.second), [callback, arg]() {
    LOG(INFO) << "Server: request #" << arg.first << " timer " << arg.second
              << " expired, sending response";
    Void result;
    callback(roo_transport::kOk, result);
    pending_requests.release();
  });
}

// This is the dispatch table of all RPC functions registered in the server
// (in our case, just our single function defined above).
//
// We use the AsyncUnaryHandler, which is a convenience wrapper that 'promotes'
// a function (void)(const Request&, response_cb) to a proper handler that the
// RPC server knows how to work with. The handler takes care of serialization
// and deserialization, and basic error handling.
FunctionTable rpc_function_table = {
    {kTimerFn, AsyncUnaryHandler<TimerArg, Void>(scheduleTimer)},
};

SerialLinkTransport<decltype(Serial1)> server_serial(Serial1);
LinkMessaging server_messaging(server_serial.transport(), kMaxPayloadSize);
RpcServer rpc_server(server_messaging, &rpc_function_table);

void server() {
  Serial1.begin(5000000, SERIAL_8N1, kPinServerRx, kPinServerTx);
  server_serial.begin();
  rpc_server.begin();
  server_messaging.begin();

  // We use the main thread to actually call the response callbacks (deliver the
  // RPC responses).
  scheduler.run();
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

SerialLinkTransport<decltype(Serial2)> client_serial(Serial2);
LinkMessaging client_messaging(client_serial.transport(), kMaxPayloadSize);
RpcClient rpc_client(client_messaging);

// Declares the stub for our single RPC function. Uses the UnaryStub helper
// which handles serialization/deserialization, basic error handling, and
// synchronous calls (as the underlying RPC layer is inherently asynchronous).
UnaryStub<TimerArg, Void> timer_stub(rpc_client, kTimerFn);

void client() {
  Serial2.begin(5000000, SERIAL_8N1, kPinClientRx, kPinClientTx);
  client_serial.begin();
  rpc_client.begin();
  client_messaging.begin();

  // Make requests in a loop, asychronously, with random delays from 10s to 20s.
  //
  // The loop quickly generates the first 5000 requests (in fact, slightly more,
  // as a few more requests get buffered in the RPC RAM buffers before the link
  // gets completely throttled). Then, at 10 seconds, as the first timers start
  // expiring, the server starts sending responses, and the client starts
  // receiving them. As the client receives responses, the server is able to
  // accept new requests again, so the system reaches a steady state where
  // requests are sent and responses received at a roughly constant rate.
  int request_idx = 0;
  while (true) {
    size_t delay_ms = (rand() % 10000) + 10000;
    TimerArg arg = std::make_pair(request_idx, delay_ms);
    long start_time = millis();
    LOG(INFO) << "Client: sending request #" << request_idx << ": " << delay_ms
              << " ms";
    timer_stub.callAsync(arg, [delay_ms, request_idx, start_time](
                                  roo_transport::Status status, Void result) {
      if (status != roo_transport::kOk) {
        LOG(INFO) << "Client: RPC failed for request " << request_idx << ": "
                  << status;
      } else {
        LOG(INFO) << "Client: RPC succeeded for request #" << request_idx
                  << " after " << (millis() - start_time)
                  << "ms (requested: " << delay_ms << " ms)";
      }
    });
    request_idx++;
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
