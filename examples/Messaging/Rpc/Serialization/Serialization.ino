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
// This example demonstrates how to serialize various kinds of data over RPC.

// Important: to run this example in the loopback mode, you need to connect the
// TX and RX pins of the two UARTs to each other using jumper wires. The pin
// numbers are defined by the constants kPinServerTx, kPinServerRx,
// kPinClientTx, and kPinClientRx below. Make sure to cross the wires, i.e. to
// connect kPinServerTx with kPinClientRx, and kPinServerRx with kPinClientTx.

#include "roo_threads.h"
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

// This is the definition of our RPC service, which essentially lists all
// supported RPC functions. In our example, we only use one function.
enum FunctionId {
  kFnIntToVoid = 0,
  kFnPairToInt = 1,
  kFnTupleToVoid = 2,
  kFnStructToVoid = 3,
};

// Any supported type can be used both as an argument and as a return value.
// Here we define a few types to demonstrate various serialization capabilities.
// For return values, in most cases we use 'Void', which indicates no return
// value. (Void can also be used as an argument type if desired).

// Simple numeric types, strings, pairs, and tuples are supported out of the
// box.

using MyPair = std::pair<uint16_t, roo::string_view>;
using MyTuple = std::tuple<uint8_t, uint32_t, roo::string_view>;

struct MyStruct {
  uint8_t a;
  uint16_t b;
  roo::string_view c;
};

// To handle an arbitrary struct, we need to explicitly define a serializer and
// deserializer for it.

namespace roo_transport {

template <>
struct Serializer<MyStruct> {
  // The serialize function should return any object that supports the
  // following interface:
  //   Status status() const;
  //   const roo::byte* data() const;
  //   size_t size() const;
  //
  // Custom serializers can use any means to serialize the data, as long as
  // they return an object supporting the above interface.
  //
  // Here, we use a helper function for serializing subsequent members.
  // The helper serializes the value of given type, using that type's
  // serializer, prepending the serialized data with its length as a 2-byte
  // big-endian unsigned integer. The serialized representation is then
  // simply a concatenation of the serialized members.
  DynamicSerialized serialize(const MyStruct& value) const {
    DynamicSerialized result;
    SerializeMemberInto(value.a, result);
    SerializeMemberInto(value.b, result);
    SerializeMemberInto(value.c, result);
    return result;
  }
};

template <>
struct Deserializer<MyStruct> {
  // The deserialize function should take a pointer to the data buffer,
  // its length, and a reference to the result object to be populated.
  // It should return a Status indicating success or failure of the
  // deserialization.
  //
  // Here, we use a helper function for deserializing subsequent members.
  // The helper reads a 2-byte big-endian length prefix, then uses the
  // appropriate deserializer to read that many bytes into the result member.
  // The data and len pointers are advanced as necessary.
  RpcStatus deserialize(const roo::byte* data, size_t len,
                        MyStruct& result) const {
    Status status;
    status = DeserializeMember(data, len, result.a);
    if (status != roo_transport::kOk) {
      return status;
    }
    status = DeserializeMember(data, len, result.b);
    if (status != roo_transport::kOk) {
      return status;
    }
    status = DeserializeMember(data, len, result.c);
    if (status != roo_transport::kOk) {
      return status;
    }
    if (len != 0) {
      // Not all input data has been consumed.
      return roo_transport::kInvalidArgument;
    }
    return roo_transport::kOk;
  }
};

}  // namespace roo_transport

static const int kMaxPayloadSize = 1024;

#if MODE == MODE_LOOPBACK || MODE == MODE_SERVER

Status fnIntToVoid(int32_t x, Void& result) {
  LOG(INFO) << "Server: Received an intToVoid request " << x;
  return kOk;
}

Status fnPairToInt(const MyPair& val, int32_t& result) {
  LOG(INFO) << "Server: Received a pairToInt request {" << val.first << ", "
            << val.second << "}. Returning 1";
  result = 1;
  return kOk;
}

Status fnTupleToVoid(const MyTuple& val, Void& result) {
  LOG(INFO) << "Server: Received a tupleToVoid request {"
            << (int)std::get<0>(val) << ", " << std::get<1>(val) << ", "
            << std::get<2>(val) << "}";
  return kOk;
}

Status fnStructToVoid(const MyStruct& val, Void& result) {
  LOG(INFO) << "Server: Received a structToVoid request {" << (int)val.a << ", "
            << val.b << ", " << val.c << "}";
  return kOk;
}

FunctionTable rpc_function_table = {
    {kFnIntToVoid, UnaryHandler<int32_t, Void>(fnIntToVoid)},
    {kFnPairToInt, UnaryHandler<MyPair, int32_t>(fnPairToInt)},
    {kFnTupleToVoid, UnaryHandler<MyTuple, Void>(fnTupleToVoid)},
    {kFnStructToVoid, UnaryHandler<MyStruct, Void>(fnStructToVoid)},
};

SerialLinkTransport<decltype(Serial1)> server_serial(Serial1);
LinkMessaging server_messaging(server_serial.transport(), kMaxPayloadSize);
RpcServer rpc_server(server_messaging, &rpc_function_table);

// This is our server's entry point.
void server() {
  Serial1.begin(5000000, SERIAL_8N1, kPinServerRx, kPinServerTx);
  server_serial.begin();
  rpc_server.begin();
  server_messaging.begin();

  while (true) {
    delay(1000);
  }
}

#endif  // MODE == MODE_LOOPBACK || MODE == MODE_SERVER

#if MODE == MODE_LOOPBACK || MODE == MODE_CLIENT

SerialLinkTransport<decltype(Serial2)> client_serial(Serial2);
LinkMessaging client_messaging(client_serial.transport(), kMaxPayloadSize);
RpcClient rpc_client(client_messaging);

// Declares the stub for our single RPC function. Uses the UnaryStub helper
// which handles serialization/deserialization, basic error handling, and
// synchronous calls (as the underlying RPC layer is inherently asynchronous).
UnaryStub<int32_t, Void> int_to_void_stub(rpc_client, kFnIntToVoid);
UnaryStub<MyPair, int32_t> pair_to_int_stub(rpc_client, kFnPairToInt);
UnaryStub<MyTuple, Void> tuple_to_void_stub(rpc_client, kFnTupleToVoid);

UnaryStub<MyStruct, Void> struct_to_void_stub(rpc_client, kFnStructToVoid);

void client() {
  // Initialize UART.
  Serial2.begin(5000000, SERIAL_8N1, kPinClientRx, kPinClientTx);
  client_serial.begin();
  rpc_client.begin();
  client_messaging.begin();

  while (true) {
    {
      Void result;
      LOG(INFO) << "Client: calling intToVoid(42)";
      Status status = int_to_void_stub.call(42, result);
      if (status == kOk) {
        LOG(INFO) << "Client: intToVoid RPC succeeded.";
      } else {
        LOG(ERROR) << "Client: intToVoid RPC failed: " << (int)status;
      }
    }

    {
      int32_t result;
      LOG(INFO) << "Client: calling pairToInt({42, Foo})";
      Status status = pair_to_int_stub.call({42, "Foo"}, result);
      if (status == kOk) {
        LOG(INFO) << "Client: pairToInt RPC succeeded. Received " << result;
      } else {
        LOG(ERROR) << "Client: pairToInt RPC failed: " << (int)status;
      }
    }

    {
      Void result;
      LOG(INFO) << "Client: calling tupleToVoid({42, 5, Bar})";
      Status status = tuple_to_void_stub.call({42, 5, "Bar"}, result);
      if (status == kOk) {
        LOG(INFO) << "Client: tupleToVoid RPC succeeded.";
      } else {
        LOG(ERROR) << "Client: tupleToVoid RPC failed: " << (int)status;
      }
    }

    {
      Void result;
      LOG(INFO) << "Client: calling structToVoid({42, 5, Bar})";
      Status status = struct_to_void_stub.call({42, 5, "Bar"}, result);
      if (status == kOk) {
        LOG(INFO) << "Client: structToVoid RPC succeeded.";
      } else {
        LOG(ERROR) << "Client: structToVoid RPC failed: " << (int)status;
      }
    }

    delay(1000);
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
