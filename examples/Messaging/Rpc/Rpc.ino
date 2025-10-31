#ifdef ROO_TESTING

#include "roo_testing/buses/uart/fake_uart.h"
#include "roo_testing/microcontrollers/esp32/fake_esp32.h"
#include "roo_io/ringpipe/ringpipe.h"

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

// This example demonstrates how to use the messaging transport to implement a
// simple single-threaded RPC system (server and client).
//
// Messaging transport is meant to support asynchronous message passing and
// RPC-style interactions, including streaming RPC, over unreliable connections
// such as UART. It works on top of the Link transport (providing reliable bidi
// streaming capabilities), but it handles connection resets more gracefully -
// it allows the client to be notified of the resulting packet loss, but
// otherwise it just reconnects and keeps going.
//
// In this example, we implement a simple but functional RPC system. Our RPC
// server is able to handle multiple RPC functions, and to return RPC errors. If
// the server gets reset during a request, that particular RPC fails, but the
// overall system keeps going, and the client can just retry the request.

// Important: to run this example in the loopback mode, you need to connect the
// TX and RX pins of the two UARTs to each other using jumper wires. The pin
// numbers are defined by the constants kPinServerTx, kPinServerRx,
// kPinClientTx, and kPinClientRx below. Make sure to cross the wires, i.e. to
// connect kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.

#include "roo_io.h"
#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/memory_input_iterator.h"
#include "roo_io/memory/memory_output_iterator.h"
#include "roo_threads.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/serial_link_transport.h"
#include "roo_transport/link/link_messaging.h"

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

// Mimics Google gRPC status codes. (In the example we only use a couple of
// them.)
enum RpcStatus {
  kRpcOk = 0,
  kRpcCancelled = 1,
  kRpcUnknown = 2,
  kRpcInvalidArgument = 3,
  kRpcDeadlineExceeded = 4,
  kRpcNotFound = 5,
  kRpcAlreadyExists = 6,
  kRpcPermissionDenied = 7,
  kRpcResourceExhausted = 8,
  kRpcFailedPrecondition = 9,
  kRpcAborted = 10,
  kRpcOutOfRange = 11,
  kRpcUnimplemented = 12,
  kRpcInternal = 13,
  kRpcUnavailable = 14,
  kRpcDataLoss = 15,
  kRpcUnauthenticated = 16
};

// A common interface for request and response messages.
class Serializable {
 public:
  virtual ~Serializable() = default;
  virtual RpcStatus serialize(roo::byte* data, size_t max_len,
                              size_t& actual_len) = 0;
  virtual RpcStatus deserialize(const roo::byte* data, size_t len) = 0;
};

// A simple implementation of Serializable for uint32_t values. We use it to
// implement our example RPC function, below. You can implement more
// sophisticated Serializable classes as needed, using roo_io memory iterators
// and read/write functions for convenience.
class SerializableUint32 : public Serializable {
 public:
  SerializableUint32(uint32_t value = 0) : value_(value) {}

  RpcStatus serialize(roo::byte* data, size_t max_len,
                      size_t& actual_len) override {
    if (max_len < 4) {
      return kRpcInvalidArgument;
    }
    roo_io::StoreBeU32(value_, data);
    actual_len = 4;
    return kRpcOk;
  }

  RpcStatus deserialize(const roo::byte* data, size_t len) override {
    if (len != 4) {
      return kRpcInvalidArgument;
    }
    value_ = roo_io::LoadBeU32(data);
    return kRpcOk;
  }

  uint32_t get() const { return value_; }

 private:
  uint32_t value_;
};

// This is the definition of our 'RPC service', which essentially lists all
// supported RPC functions. In our example, we only use one function.
enum FunctionId {
  kSquareFn = 0,
};

// This constant defines the maximum size of the payload requests and responses.
// The sizes are constrained only by the available RAM (as the requests and the
// responses must fit entirely in RAM during the RPC call).
static const int kMaxPayloadSize = 128;

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

// This is the implementation of our example RPC function. It receives a
// uint32_t value in the request, computes its square, and returns it in the
// response.
RpcStatus handleSquareFn(const roo::byte* request, size_t request_len,
                         roo::byte* response, size_t max_response_len,
                         size_t& actual_response_len) {
  SerializableUint32 req;
  RpcStatus status = req.deserialize(request, request_len);
  if (status != kRpcOk) {
    return status;
  }
  uint32_t num = req.get();
  Serial.printf("Server: Received a request to square %d\n", num);

  // Pretend to be doing something useful.
  delay(1000);

  uint32_t num_square = num * num;

  Serial.printf("Server: Completing the request to square %d\n", num);
  SerializableUint32 resp(num_square);
  return resp.serialize(response, max_response_len, actual_response_len);
}

using RpcHandlerFn = std::function<RpcStatus(
    const roo::byte* request, size_t request_len, roo::byte* response,
    size_t max_response_len, size_t& actual_response_len)>;

// This is the 'dispatch table' of all RPC functions registered in the server
// (in our case, just our single function defined above).
RpcHandlerFn rpc_function_table[] = {
    handleSquareFn,  // kSquareFn
};

// Generic request handler function that dispatches requests to specific RPC
// handlers based on the function ID in the request, and sends back the
// response, along with the RPC status.
//
// Our convention is that the first byte of the request payload contains the
// function ID, and the rest is the serialized request message. Similarly, the
// first byte of the response payload contains the RPC status, and the rest is
// the serialized response message.
void handleRequest(const roo::byte* data, size_t len,
                   Messaging::Channel& messaging) {
  uint8_t function_id = (uint8_t)data[0];
  if (function_id >=
      sizeof(rpc_function_table) / sizeof(rpc_function_table[0])) {
    // Invalid function ID.
    roo::byte err[1] = {(roo::byte)kRpcInvalidArgument};
    messaging.send(err, 1);
    return;
  }
  std::unique_ptr<roo::byte[]> response(new roo::byte[kMaxPayloadSize]);
  RpcHandlerFn& request_handler = rpc_function_table[function_id];
  size_t response_len = 0;
  // Dispatch the request to the appropriate handler.
  RpcStatus status = request_handler(data + 1, len - 1, response.get() + 1,
                                     kMaxPayloadSize - 1, response_len);
  response[0] = (roo::byte)status;
  if (status != kRpcOk) {
    Serial.printf("Server: RPC %d failed with status %d\n", function_id,
                  status);
    // Send back only the status byte.
    messaging.send(response.get(), 1);
  } else {
    // Send back the status byte + the serialized response.
    messaging.send(response.get(), response_len + 1);
  }
}

SerialLinkTransport<decltype(Serial1)> server_serial(Serial1);
LinkMessaging server_messaging(server_serial.transport(), kMaxPayloadSize);
std::unique_ptr<Messaging::Channel> server_channel(
    server_messaging.newChannel(0));

// Here we are registering the function that handles incoming messages. Since we
// don't care about clients resetting, we can inherit from SimpleReceiver (which
// provides a no-op reset() handler).
Messaging::SimpleReceiver server_receiver([](const roo::byte* data,
                                             size_t len) {
  handleRequest(data, len, *server_channel);
});

// This is our server's entry point.
void server() {
  // Initialize UART.
  Serial1.begin(5000000, SERIAL_8N1, kPinServerRx, kPinServerTx);

  // Initialize the reliable serial transport.
  server_serial.begin();

  // Initialize the RPC server channel to use our request handler.
  server_channel->setReceiver(server_receiver);

  // Start the 'RPC' server. Our receiver gets registered to trigger (and call
  // handleRequest) every time a new message is received. This happens in a
  // newly created singleton thread.
  server_messaging.begin();

  // Just idle - we don't need the loop thread anymore.
  while (true) {
    delay(1000);
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

SerialLinkTransport<decltype(Serial2)> client_serial(Serial2);
LinkMessaging client_messaging(client_serial.transport(), 128);
std::unique_ptr<Messaging::Channel> client_channel(
    client_messaging.newChannel(0));

// The RPC client is an interface for making RPC calls. It implements the
// Messaging::Receiver interface to receive responses from the server.
//
// In our implementation, the call() method is synchronous - it blocks until the
// response is received (or an error occurs). It uses a condition variable to
// wait for the response in a thread-safe manner. Only one RPC can be active at
// any given time.
//
// (This is OK for a single-threaded client application, as in this example. For
// a multi-threaded client, you would need to implement a more sophisticated
// client that can handle multiple concurrent RPCs, e.g., by using unique
// request IDs to match responses to requests.)
class RpcClient : public Messaging::Receiver {
 public:
  RpcStatus call(FunctionId fn, Serializable& request, Serializable& response) {
    roo::unique_lock<roo::mutex> lock(mutex_);
    response_ = &response;
    status_ = kRpcOk;
    std::unique_ptr<roo::byte[]> send_buffer(new roo::byte[max_payload_size_]);
    // Specify the function ID to be called as the first byte of the request.
    send_buffer[0] = (roo::byte)fn;
    size_t request_size = 0;
    // Serialize the request message after the function ID.
    status_ = request.serialize(send_buffer.get() + 1, max_payload_size_ - 1,
                                request_size);
    // Bail in case the argument serialization failed.
    if (status_ != kRpcOk) {
      return status_;
    }

    // Everything looks good - send the request.
    client_channel->send(send_buffer.get(), request_size + 1);

    // Wait for the response to be received in the received() callback.
    do {
      cv_.wait(lock);
    } while (response_ != nullptr);
    return status_;
  }

  // Gets called by the messaging framework when a new (response) message is
  // received.
  void received(const roo::byte* data, size_t len) override {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (response_ == nullptr) {
      LOG(WARNING) << "Client: unexpected response received";
      return;
    }
    status_ = (RpcStatus)(data[0]);
    if (status_ != kRpcOk) {
      response_ = nullptr;
      cv_.notify_one();
      return;
    }
    status_ = response_->deserialize((const roo_io::byte*)data + 1, len - 1);
    response_ = nullptr;
    cv_.notify_one();
  }

  // Gets called by the messaging framework when the connection is reset.
  void reset() override {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (response_ != nullptr) {
      status_ = kRpcUnavailable;
      response_ = nullptr;
      cv_.notify_one();
    }
  }

 private:
  Serializable* response_ = nullptr;
  RpcStatus status_ = kRpcOk;
  mutable roo::mutex mutex_;
  mutable roo::condition_variable cv_;
  int max_payload_size_ = 128;
};

RpcClient rpc_client;

// Helper 'stub' function to call the square RPC.
RpcStatus callSquareRpc(RpcClient& rpc, uint32_t request, uint32_t& response) {
  SerializableUint32 req(request);
  SerializableUint32 resp;
  RpcStatus status = rpc.call(kSquareFn, req, resp);
  if (status == kRpcOk) {
    response = resp.get();
  }
  return status;
}

void client() {
  // Initialize UART.
  Serial2.begin(5000000, SERIAL_8N1, kPinClientRx, kPinClientTx);

  // Initialize the reliable serial transport.
  client_serial.begin();

  // Initialize the RPC client channel to be ready to receive response messages.
  client_channel->setReceiver(rpc_client);

  // Start the 'RPC' client. It creates a singleton thread to handle incoming
  // response messages.
  client_messaging.begin();

  // Make requests in a loop.
  while (true) {
    for (uint32_t request = 1; request < 1000; request++) {
      uint32_t response;
      RpcStatus status = callSquareRpc(rpc_client, request, response);
      if (status != kRpcOk) {
        Serial.printf("RPC failed: %d\n", status);
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

void loop() {}
