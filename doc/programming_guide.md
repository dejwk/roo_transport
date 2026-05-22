# Programming guide

> This guide is written as practical documentation and is organized from the
> most common application-level use cases toward extension and maintenance
> topics.

## Before you start

### What problem does roo_transport solve?

`roo_transport` is a small communication stack for point-to-point embedded
links, especially UART. Use it when two peers need more than raw serial bytes
but less than a full network stack: packet framing, integrity checking,
reliable streaming, reconnect-aware messaging, or simple RPC.

Typical application examples include a UI microcontroller talking to a separate
sensor or actuator board, a main MCU delegating one subsystem to a coprocessor,
or two boards connected by a cheap cable that still need reliable command/response
behavior.

Raw UART has two important limitations. First, it is only a byte pipe: it does
not preserve message boundaries, and a dropped or corrupted byte can throw a
parser out of sync. Second, even if you add reliable delivery on top, peer
resets and cable disconnects do not disappear. If the other side reboots halfway
through an exchange, the application still needs a policy for what happens next.

That is why `roo_transport` is not presented as a "better UART" that hides all
failures. Instead, it provides a ladder of abstractions. At the low end, you can
keep working with packets or streams. At the high end, you can use messaging or
RPC and let the library surface reconnect boundaries explicitly. A hard-wired
appliance that should simply reboot on peer loss can use `connectOrDie()`. A
more resilient application can detect resets and rebuild its state cleanly.

This is not a TCP/IP stack. It does not implement IP, it does not expose socket
APIs, and it will not interoperate directly with TCP/IP sockets. That tradeoff
is deliberate: the protocol is custom, UART-oriented, and designed to stay
light on RAM. In common configurations, a realistic footprint is roughly 10-20
KB per endpoint. In return, it performs very well on the hardware it targets.
On ESP32, the current implementation reaches about 3.4 Mbps over short wires
with sub-millisecond round-trip latency, and about 2.5 Mbps with 0.92 ms p99
round-trip latency even over a 7.5 m unshielded four-wire cable with heavy
packet loss.

The stack has four main layers:

- **Packets** are UDP-like datagrams with integrity but no delivery guarantee.
- **Links** are TCP-like reliable byte streams.
- **Messaging** adds explicit message boundaries and reconnect handling on top
  of a link.
- **RPC** builds typed request/response calls on top of messaging.

## Part 1: Getting started

### Connecting two peers

Most examples in this guide assume a direct UART link between two peers. The
minimum physical connection is TX, RX, and ground. TX and RX must be crossed:
TX on one side goes to RX on the other, and vice versa.

There are two convenient ways to run the examples:

- Use two separate boards connected by UART wires.
- Use one board in loopback mode with two UART peripherals wired to each other.

The bundled examples use these pin pairs in loopback mode:

- ESP32: `Serial1` uses TX 27 / RX 14, and `Serial2` uses TX 25 / RX 26.
- RP2040: `Serial1` uses TX 12 / RX 13, and `Serial2` uses TX 4 / RX 5.

These are only example pin assignments. The transport layer does not require
those exact pins.

Although UART is the main target, the higher-level abstractions are not limited
to raw hardware serial. The packet, link, messaging, and RPC layers can also be
built on top of other stream-backed or packet-backed transports. UART is simply
the easiest starting point.

If you want to begin with a complete runnable example before reading further,
start with [Packets](../examples/Packets/Packets.ino) or
[ReliableSerial loopback](../examples/ReliableSerial/Loopback/Loopback.ino).

### Choosing the right abstraction level

The library is easiest to use when you start at the layer that already matches
the shape of your application data.

| Layer | Mental model | Good fit | Typical examples |
| --- | --- | --- | --- |
| Packets | UDP-like datagrams | Occasional loss is acceptable and the newest sample matters most | sensor readings, telemetry beacons, button states |
| Link | TCP-like byte stream | You already think in terms of bytes flowing over a stream | tunneling an existing binary protocol, exposing a stream-like device service |
| Messaging | Reliable discrete messages | Your application already has message boundaries | controller commands, acknowledgements, state synchronization |
| RPC | Remote function calls | The remote side naturally looks like a service API | `getTemperature()`, `setBrightness()`, `readConfig()`, `runCalibration()` |

If your application already has clear message boundaries, start at messaging.
If it already has a request/response service model, start at RPC. If occasional
loss is acceptable, packets may be enough. In practice, many users will skip
the lower layers entirely and start at either messaging or RPC.

### Basic reliable link setup

The quickest path to a useful connection is the reliable link layer. It gives
you a pair of streams, one for reading and one for writing, while hiding packet
framing, retransmissions, and connection handshakes.

On Arduino targets, the easiest entry point is one of the `ReliableSerial*`
adapters. They wrap a UART-backed transport and expose a connected `LinkStream`
once the peer is ready.

The following example shows one side of a reliable link. The peer needs to run
compatible code on the other end of the UART connection.

```cpp
#include "roo_io.h"
#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/reliable_serial.h"

using namespace roo_transport;

#if defined(ESP_PLATFORM)
constexpr int kTxPin = 27;
constexpr int kRxPin = 14;
constexpr uint32_t kBaudRate = 5000000;
#elif defined(ARDUINO_ARCH_RP2040)
constexpr int kTxPin = 12;
constexpr int kRxPin = 13;
constexpr uint32_t kBaudRate = 115200;
#endif

ReliableSerial1 reliable_serial;

void setup() {
  Serial.begin(115200);

#if defined(ESP_PLATFORM)
  Serial1.setRxBufferSize(4096);
  Serial1.begin(kBaudRate, SERIAL_8N1, kRxPin, kTxPin);
#elif defined(ARDUINO_ARCH_RP2040)
  Serial1.setPinout(kTxPin, kRxPin);
  Serial1.setFIFOSize(1024);
  Serial1.begin(kBaudRate, SERIAL_8N1);
#endif

  reliable_serial.begin();
  LinkStream link = reliable_serial.connectOrDie();

  roo_io::OutputStreamWriter out(link.out());
  roo_io::InputStreamReader in(link.in());

  out.writeBeU32(123);
  out.writeString("Hello, peer!\n");
  out.flush();

  uint32_t reply_id = in.readBeU32();
  std::string reply = in.readString();
  Serial.printf("Reply %u: %s", reply_id, reply.c_str());
}

void loop() {}
```

There are four important steps here.

First, initialize the UART itself. `roo_transport` does not hide the board-
specific serial setup, because pin selection, FIFO sizing, and UART buffering
are hardware concerns.

Second, start the reliable transport with `begin()`. This starts the transport
machinery that performs reliable sending in the background.

Third, connect to the peer. `connect()` waits for the handshake to complete and
returns a connected link. `connectOrDie()` does the same thing, but treats a
later peer reset as fatal. That is convenient for dedicated point-to-point
links where a reset means the whole application should restart. If you want to
recover gracefully, prefer `connect()` or `connectAsync()`.

Fourth, use `link.out()` and `link.in()` as normal `roo_io` streams. At that
point you are working with a reliable byte stream, not raw UART bytes.

For a complete two-sided example, including loopback wiring and simple sender /
receiver threads, see
[ReliableSerial loopback](../examples/ReliableSerial/Loopback/Loopback.ino).

#### Arduino adapters

On ESP32, the library provides `ReliableSerial`, `ReliableSerial1`, and
`ReliableSerial2`, which wrap `Serial`, `Serial1`, and `Serial2`
respectively. On RP2040, the library provides `ReliableSerial1` and
`ReliableSerial2`.

The ESP32 and RP2040 setup differs slightly:

- ESP32 typically uses `SerialX.begin(baud, SERIAL_8N1, rx, tx)` and often
  benefits from an explicit RX buffer size.
- RP2040 uses `setPinout()`, optionally `setFIFOSize()`, and then `begin()`.

If you are not using one of the convenience serial adapters, the generic Arduino
entry point is `LinkStreamTransport`, which works on any `Stream`. In that case
you are responsible for calling `tryReceive()` or `receive()` so incoming
packets are processed.

#### Link objects and lifecycle

`Link` is the core connection object returned by `LinkTransport`. In Arduino
examples you will usually see `LinkStream`, which is the stream-oriented wrapper
returned by the serial adapters.

The main lifecycle methods are:

- `status()`: reports whether the link is idle, connecting, connected, or
  broken.
- `awaitConnected()`: waits for a pending asynchronous connection to either
  succeed or fail.
- `disconnect()`: closes the current connection.
- `streamId()`: returns the current local stream id, which changes across
  reconnects.

The states are straightforward in practice. `kIdle` means no active connection.
`kConnecting` means the handshake is in progress. `kConnected` means reads and
writes may proceed. `kBroken` means the peer or transport failed and the link
can no longer be used.

Use `connectAsync()` when your application wants to start other work while the
link is coming up. The common pattern is to call `connectAsync()`, continue
initializing, and then use `awaitConnected()` with or without a timeout before
the first real exchange.

### Reliable messaging

If your application already thinks in terms of complete records or commands,
the messaging layer is usually a better fit than a raw byte stream. It sits on
top of a reliable link and restores explicit message boundaries.

The main concrete implementation is `LinkMessaging`. You give it a reliable
transport and the maximum message size you are prepared to receive. Then you
register a receiver, start the receive loop, and call `send()` whenever you
want to transmit a message.

Assuming the UART and `ReliableSerial1` transport have already been initialized
as in the previous section, the new pieces look like this:

```cpp
#include "roo_io/data/read.h"
#include "roo_io/data/write.h"
#include "roo_io/memory/memory_input_iterator.h"
#include "roo_io/memory/memory_output_iterator.h"
#include "roo_logging.h"
#include "roo_transport/link/link_messaging.h"

using namespace roo_transport;

constexpr int kMaxPayloadSize = 32;

LinkMessaging messaging(reliable_serial, kMaxPayloadSize);

Messaging::SimpleReceiver receiver(
    [](Messaging::ConnectionId connection_id, const roo::byte* data,
       size_t len) {
      roo_io::MemoryIterator in(data, data + len);
      roo::string_view msg = roo_io::ReadStringView(in, 24);
      LOG(INFO) << "Received on connection " << connection_id << ": " << msg;
    });

void startMessaging() {
  messaging.setReceiver(receiver);
  messaging.begin();

  roo::byte buf[32];
  roo_io::MemoryOutputIterator out(buf, buf + sizeof(buf));
  roo_io::WriteString(out, "Hello!");
  messaging.send(buf, out.ptr() - buf);
}
```

The important difference from the raw link layer is that one call to `send()`
produces one complete received message. You do not need to invent your own
length prefixes or delimiters.

`ConnectionId` identifies the current logical connection instance. That matters
when reconnects are possible. If a message should be treated as a reply on the
same connection that produced the incoming request, use
`sendContinuation(connection_id, ...)`. If the peer has reconnected in the
meantime, that continuation send fails instead of silently attaching the reply
to a new connection.

The `kMaxPayloadSize` constructor argument is a real RAM tradeoff. It must be
large enough to hold your biggest incoming message, but larger values consume
more memory. The simple example uses 32 bytes because its payloads are tiny.

For a full working example with both sides sending periodic messages, see
[Messaging simple](../examples/Messaging/Simple/Simple.ino).

### First RPC service

RPC is the highest-level API in the library. Use it when the remote side really
does look like a service with named operations. Instead of sending raw messages
such as "opcode 7, payload X", you define a function id, a request type, and a
response type.

The server side consists of four pieces:

- an enum of function ids,
- a handler function,
- a `FunctionTable` mapping ids to handlers,
- and an `RpcServer` attached to a `Messaging` transport.

The smallest useful example looks like this:

```cpp
#include "roo_transport/link/arduino/reliable_serial.h"
#include "roo_transport/link/link_messaging.h"
#include "roo_transport/rpc/client.h"
#include "roo_transport/rpc/rpc.h"
#include "roo_transport/rpc/server.h"

using namespace roo_transport;

enum FunctionId {
  kSquareFn = 0,
};

RpcStatus calcSquare(uint32_t x, uint32_t& result) {
  result = x * x;
  return kOk;
}

FunctionTable rpc_function_table = {
    {kSquareFn, UnaryHandler<uint32_t, uint32_t>(calcSquare)},
};

ReliableSerial1 server_serial;
LinkMessaging server_messaging(server_serial, 32);
RpcServer rpc_server(server_messaging, &rpc_function_table);

void startRpcServer() {
  server_serial.begin();
  rpc_server.begin();
  server_messaging.begin();
}
```

The client side uses `RpcClient` plus a typed `UnaryStub`:

```cpp
ReliableSerial2 client_serial;
LinkMessaging client_messaging(client_serial, 32);
RpcClient rpc_client(client_messaging);
UnaryStub<uint32_t, uint32_t> square_stub(rpc_client, kSquareFn);

void startRpcClient() {
  client_serial.begin();
  rpc_client.begin();
  client_messaging.begin();
}

RpcStatus callSquare(uint32_t request, uint32_t& response) {
  return square_stub.call(request, response);
}
```

Call `startRpcClient()` once during setup, then call `callSquare()` as often as
needed during normal operation.

`UnaryHandler<Request, Response>` is a convenience adapter. It takes a normal
typed C++ function and handles the RPC-side serialization, deserialization, and
reply framing. `UnaryStub<Request, Response>` performs the same kind of wrapping
on the client side.

This first example uses plain integers, but the same structure works for richer
types. When your request and response values stop being trivial scalars, the
next example to read is
[RPC serialization](../examples/Rpc/Serialization/Serialization.ino), which
shows custom serializers and deserializers.

If you want non-blocking client-side calls or asynchronous server-side work,
look at `callAsync()` on `UnaryStub` and `AsyncUnaryHandler` on the server side.
The walkthrough for that lives in
[RPC asynchronous](../examples/Rpc/Asynchronous/Asynchronous.ino).

For the full square example used here, see
[RPC simple](../examples/Rpc/Simple/Simple.ino).

### Example roadmap

The examples are easiest to understand in this order:

- [Packets](../examples/Packets/Packets.ino): the lowest layer, useful when
  integrity matters but delivery does not.
- [ReliableSerial loopback](../examples/ReliableSerial/Loopback/Loopback.ino):
  a reliable byte stream over two UARTs on the same board.
- [Messaging simple](../examples/Messaging/Simple/Simple.ino): reliable
  message exchange with automatic reconnect handling.
- [Messaging multiplexing](../examples/Messaging/Multiplexing/Multiplexing.ino):
  multiple logical channels over one messaging transport.
- [RPC simple](../examples/Rpc/Simple/Simple.ino): the smallest end-to-end RPC
  service.
- [RPC serialization](../examples/Rpc/Serialization/Serialization.ino): custom
  request and response types.
- [RPC asynchronous](../examples/Rpc/Asynchronous/Asynchronous.ino): async
  request handling and completion callbacks.

## Part 2: Features in depth

### Packets

The packet layer is the foundation of the rest of the stack. The core
interfaces are `PacketSender` and `PacketReceiver`: one side emits whole packet
payloads, the other side delivers whole packet payloads. If your transport is
already packet-oriented, these interfaces may be all you need.

For byte streams such as UART, the library provides `PacketSenderOverStream` and
`PacketReceiverOverStream`. These adapters add COBS framing and a 32-bit hash so
the receiver can recover packet boundaries and reject corrupted data even when
the underlying serial stream loses or mangles bytes. The payload limit is 250
bytes per packet.

It is reasonable to think of this layer as loosely analogous to UDP or raw IP
datagrams: one send produces one bounded unit, and delivery is not guaranteed.
That is only an analogy. These packets are custom to `roo_transport`; they are
not IP packets and they are not compatible with UDP or socket APIs.

The guarantees are intentionally simple:

- delivered packets are intact,
- corrupted packets are dropped,
- packets may be lost,
- packets may arrive out of order.

That makes the packet layer a good fit for periodic sensor values, telemetry
samples, or state updates where the newest information matters more than every
single intermediate value. The
[Packets example](../examples/Packets/Packets.ino) demonstrates this model with
a simple temperature-reading stream. If the wire is disconnected, updates stop.
When the wire comes back, future packets are delivered again without any notion
of session continuity.

At this layer the receiver API gives you a choice between `tryReceive()` and
`receive()`. Use `tryReceive()` when packet handling must be integrated into a
larger event loop. Use `receive()` when a dedicated task or thread can block
waiting for the next valid packet.

### Reliable link transport

`LinkTransport` builds a reliable, ordered, bidirectional byte stream on top of
the packet layer. Conceptually it is the closest thing in the library to a
small TCP-like transport. It handles connection handshakes, acknowledgements,
retransmissions, and flow control so that callers can read and write bytes
instead of manually reassembling packets.

Again, the TCP comparison is only a mental model. `LinkTransport` is not TCP.
There are no sockets, no ports, no IP addressing, and no interoperability with
host networking stacks. The goal is narrower: a lightweight reliable link for
one peer talking directly to another peer.

The important operational rule is that every incoming packet from the lower
transport must be passed into `processIncomingPacket()`. On Arduino,
`ReliableSerial*` and related helpers do this for you. If you build on top of a
generic packet source, that forwarding step is your responsibility.

`begin()` starts the background send loop. `connect()` initiates a connection
and waits for the handshake to complete. `connectAsync()` returns immediately
with a link in `kConnecting` state. `disconnect_fn`, if supplied, is invoked
when that specific link instance is later broken.

The important design point is that reliability does not mean failures vanish.
If the peer resets, the current link is broken. In-flight stream state is no
longer valid, and the next connection is a new connection, not a seamless
continuation of the old one. That is why the API exposes `LinkStatus` and
`streamId()` instead of pretending the transport is just a better UART. For a
dedicated hard-wired appliance, `connectOrDie()` is often the right policy. For
applications that expect peer restarts, use `connectAsync()`, `awaitConnected()`,
status checks, and higher-level reset handling.

If you want a generic Arduino `Stream` entry point instead of the UART-specific
adapters, use `LinkStreamTransport`. It wraps packet-over-stream framing plus
`LinkTransport` and exposes the same `LinkStream` abstraction.

### Messaging semantics

`Messaging` sits above the reliable link and restores explicit message
boundaries. This is often the sweet spot for application protocols, because the
transport still guarantees ordered delivery while the application no longer has
to delimit records inside a byte stream.

`LinkMessaging` is the standard implementation. Internally it rides on a link,
adds a length prefix for each message, and runs a background receive loop that
reconnects as needed. From the caller's perspective, one `send()` produces one
received message. That is usually much simpler than inventing a private framing
format on top of `Link::out()`.

Messaging reliability applies only within a healthy connection. Messages are
delivered in order while the connection is intact, but reconnects still matter.
If the peer resets, the current conversation ends. The receiver gets a
`reset(connection_id)` callback, and any response that was meant for the old
connection must not be silently attached to the new one.

That is the purpose of `ConnectionId` and `sendContinuation()`. When you receive
a request and want to reply on the same logical connection, keep the
`connection_id` and send the reply with `sendContinuation(connection_id, ...)`.
If a reconnect happened in the meantime, that send fails instead of sending a
stale response on the wrong session.

One practical constraint is that a `Messaging` object has a single receiver.
Call `setReceiver()` before `begin()`, and assume that object is the sole owner
of inbound messages for that transport. If you need multiple independent users
of the same underlying link, use multiplexing.

`max_recv_packet_size` is also important. `LinkMessaging` allocates a receive
buffer of that size, so it should be large enough for your biggest expected
message but no larger than necessary. The
[Messaging simple](../examples/Messaging/Simple/Simple.ino) example uses 32
bytes because its payloads are tiny.

### Multiplexing logical channels

`MuxMessaging` lets several independent logical protocols share one underlying
`Messaging` transport. Each `MuxMessaging::Channel` behaves like its own
`Messaging` instance, but all channels ride over the same physical link.

Internally, multiplexing is intentionally simple: one byte of message header is
used as the channel id, so one transport can host up to 256 logical channels.
That is usually plenty for embedded applications.

This is useful whenever one cable or UART link carries several independent
kinds of traffic. For example, one channel can carry UI events, another can
carry configuration requests, and a third can carry debug or telemetry traffic.
Without multiplexing, you would have to build your own demultiplexing header
inside one larger application protocol. With `MuxMessaging`, each subsystem can
keep its own receiver and message format.

Channel lifetime matters. A `Channel` registers itself with the mux on
construction and unregisters on destruction, so channel ids must remain unique
within one `MuxMessaging` instance. Resets are propagated to every registered
channel, which keeps reconnect behavior consistent across the whole shared
transport.

See [Messaging multiplexing](../examples/Messaging/Multiplexing/Multiplexing.ino)
for the intended usage pattern.

### RPC details

The RPC layer turns a messaging transport into a request/response service. Each
request carries a function id that names the target operation and a stream id
that identifies that specific call instance. Responses echo the stream id so the
client can route the result to the right waiting call.

The current implementation is unary only: one request payload and one response
payload. The wire format has room for more general streaming concepts, but the
public API today is intentionally focused on unary calls.

On the client side, the lowest-level entry points are `RpcClient::sendUnaryRpc()`
and `sendUnaryRpcWithTimeout()`. Most users will not call those directly.
Instead, they will use `UnaryStub<Request, Response>`, which handles typed
serialization, waits for the unary response in `call()`, and exposes a
non-blocking `callAsync()` variant.

On the server side, `RpcServer` dispatches incoming requests through a
`FunctionTable`. `UnaryHandler<Request, Response>` is the simplest way to adapt
a normal typed C++ function into the raw RPC server interface. When the server
work itself is asynchronous, `AsyncUnaryHandler` lets you complete the call
later via a callback.

`RpcStatus` tells you what kind of failure occurred. A transport problem or
reset typically becomes `kUnavailable`. Malformed input or deserialization
problems generally become `kInvalidArgument`. Calling an unknown function id
produces `kUnimplemented`. Your own business logic can return any other
appropriate status.

Because RPC rides on top of `Messaging`, reconnects still matter. If the
underlying connection resets before a pending client call completes, the client
callback is completed with `kUnavailable`. On the server side, pending request
state is dropped on reconnect so stale responses are not sent on a new
connection.

One practical consequence of the `Messaging` design is that a given messaging
instance can only feed one receiver at a time. In practice that means a single
`RpcClient` or a single `RpcServer` owns each messaging transport. If you need
multiple independent RPC services on one physical connection, multiplex below
the RPC layer rather than trying to stack several RPC dispatchers on one
`Messaging` object.

### Serialization

RPC is typed because the library uses `Serializer<T>` and `Deserializer<T>`
templates to translate between C++ values and byte payloads. Basic types are
already supported, including scalars, strings, `Void`, and common aggregates
such as pairs and tuples.

When a request or response type is application-specific, define a custom
`Serializer<T>` and `Deserializer<T>`. The simplest pattern is exactly the one
shown in [RPC serialization](../examples/Rpc/Serialization/Serialization.ino):
serialize each member in a stable order and deserialize it in the same order.

For serializers, the minimum contract is straightforward: produce an object that
reports `status()`, `data()`, and `size()`. For deserializers, consume the byte
buffer and return an appropriate `RpcStatus`.

Error handling is different on the two sides. If request serialization fails on
the client, no RPC is sent at all. If request deserialization fails on the
server, the server sends an RPC failure response instead of invoking the
handler. That separation is useful because it cleanly distinguishes "I could not
build a valid request" from "the remote side rejected a request I did send".

For long-lived systems, it is worth treating serialization order as wire-format
ABI. Once two devices depend on a serialized type, changing field order or
encoding rules is a protocol change, not just a refactor.

### Buffer sizing and performance

The most important performance knob in the reliable link layer is
`LinkBufferSize`. It controls the size of the reliable send and receive windows
and ranges from 256 bytes up to 1 MB.

The default, 4 KB in each direction, is a sensible starting point for many
applications. Reduce it when RAM is tight and traffic volume is modest. Increase
it when the UART is fast, traffic is bursty, or you want to keep more data in
flight before the sender stalls.

Remember that these buffers are not the same thing as the packet size. Packets
still top out at 250 bytes, but the reliable link needs larger windows so it
can pipeline multiple packets, absorb bursts, and retransmit efficiently.
Messaging adds its own independent memory knob through `max_recv_packet_size`.

In practice, tuning usually happens in this order:

- choose a reasonable link window size,
- size UART RX buffers or FIFOs appropriately for the platform,
- only then raise baud rate or message size expectations.

On ESP32, the README numbers are a good reference point: about 3.4 Mbps on
short wires and about 2.5 Mbps over a much noisier 7.5 m cable. Treat those as
measurements, not guarantees. Actual results depend on UART configuration, task
scheduling, cable quality, and how quickly your application drains received
data.

On ESP32 specifically, it is often worth increasing the underlying serial RX
buffer with `SerialX.setRxBufferSize()`. On RP2040, FIFO sizing and receive task
responsiveness matter more than raw UART capability. In both cases, if you see
throughput plateaus or stalls, check platform UART buffering before assuming the
link protocol itself is the bottleneck.

### Error handling and observability

At the link layer, the main status surface is `LinkStatus`. `kConnected` means
normal operation. `kConnecting` means the handshake is still in progress.
`kBroken` means the current connection is gone and any in-flight state tied to
that connection is no longer valid. Whether that is fatal or retryable depends
on your application policy, not on the transport itself.

In simple dedicated devices, treating peer loss as fatal is often the right
answer, which is why `connectOrDie()` exists. In more resilient systems, a
broken connection is usually retryable: reconnect, rebuild any peer-local
state, and resume work from a clean boundary.

At the messaging layer, a plain `send()` returning `false` generally means the
transport is closed or unable to accept the message. `sendContinuation()`
returning `false` has a stronger meaning: the connection it refers to is no
longer current, so the reply must not be sent as if nothing happened.

At the RPC layer, `RpcStatus` gives you a useful separation between transport
and application errors:

- `kUnavailable`: the link failed or the peer reset; usually retryable.
- `kInvalidArgument`: malformed or unserializable input; fix the caller or the
  serializer.
- `kUnimplemented`: the server does not know that function id.
- application-defined statuses such as `kFailedPrecondition` or
  `kPermissionDenied`: the request reached the server, but the server refused it
  for domain-specific reasons.

For counters, `LinkTransport::StatsMonitor` exposes cumulative packet counts for
sent, delivered, and received packets. At the packet-over-stream layer,
`PacketReceiverOverStream` also exposes byte counters that can help diagnose
framing loss or corruption.

Logging should usually focus on connection transitions, retries, and hard
failures. The examples print much more than a production system normally would,
because their job is to make protocol behavior visible. In a real application,
it is usually better to log reconnects and error summaries than to log every
single message or RPC.

## Part 3: Advanced topics

### Concurrency and lifecycle rules

The transport stack is intentionally layered, but the concurrency model is not
uniform across all layers. Some pieces are passive adapters and only run when
you call into them. Others create background work and will invoke callbacks on
threads or event tasks that are not your sketch's main loop.

At the bottom, the packet layer is passive. `PacketSenderOverStream` sends when
you call `send()`. `PacketReceiverOverStream` delivers packets in the context of
whoever calls `tryReceive()` or `receive()`. `MuxMessaging` is also passive; it
does not create threads, it only dispatches messages it receives from an
underlying `Messaging` instance.

The link layer is where background work begins. `LinkTransport::begin()` starts
the internal send loop. On RP2040, the `ReliableSerial*` adapter also starts a
high-priority receive thread that repeatedly drains the UART. On ESP32, the
serial adapter relies on Arduino serial receive callbacks instead of a polling
thread, so incoming packet processing runs on the Arduino serial event task.

`LinkMessaging::begin()` adds another background thread. That thread reads from
the reliable link, reconstructs complete messages, and invokes the registered
receiver. As a result, a `Messaging::Receiver`, an RPC response callback, and an
RPC server handler should all be treated as background-context code, not as code
running on `loop()`.

That has two practical consequences.

- Keep callbacks short. If a receiver or RPC handler blocks for a long time, it
  can stall further receive processing.
- If the real work is expensive, capture the message, enqueue it, and hand it
  off to a worker task or your main application loop.

The safest startup order is bottom-up.

1. Initialize the hardware or stream medium.
2. Start the transport layer, for example with `ReliableSerial::begin()` or
   `LinkTransport::begin()`.
3. Register any messaging receivers, or call `RpcServer::begin()` /
   `RpcClient::begin()` so the dispatchers are installed.
4. Start `LinkMessaging::begin()` so the receive loop starts only after the
   receiver side is ready.

Shutdown, when your application needs it, should happen in the reverse
direction. Stop high-level message processing first, then stop the transport.
For example, end an RPC client or server, then call `LinkMessaging::end()`, and
finally stop the transport where that transport exposes an explicit shutdown
API. The examples often omit shutdown only because many embedded sketches run
forever.

`LinkMessaging::end()` is especially important for clean shutdown. It breaks the
current link, wakes any blocked senders, and joins its receive thread. That is
why the test suite explicitly checks that `end()` unblocks a pending send.

Reconnect-safe state handling is equally important. Any state tied to a
connection should be discarded on reset. At the link layer that means treating a
new `streamId()` as a new session. At the messaging layer it means honoring
`reset(connection_id)`. At the RPC layer it means assuming pending calls may be
cancelled with `kUnavailable` and rebuilding any remote-side conversational
state after reconnect.

### Testing and debugging

The repository uses Bazel for its main test and build workflow. The basic local
commands are:

```sh
bazel build //...
bazel test //... --test_output=errors
bazel test --config=asan //... --test_output=errors
```

The CI workflow runs the same three steps: normal build, normal test, and an
address-sanitized build and test pass. If you touch transport internals,
especially buffer management or threading code, run the ASAN configuration.

The test targets are split by concern.

- `packets_over_stream_test` exercises framing and integrity handling with
  `PacketSenderOverStream` and `PacketReceiverOverStream`.
- `link_transport_test` exercises connection setup, streaming behavior,
  reconnect behavior, and liveness properties of the reliable link.
- `link_messaging_test` exercises message delivery, reset behavior,
  multiplexing, and shutdown edge cases in `LinkMessaging`.
- `serial_link_transport_test` uses a fake ESP32 UART environment to verify the
  Arduino-facing transport adapters.

The packet tests are a good example of how to test low-level transport behavior
without hardware. They use in-memory ring pipes plus a `NoisyOutputStream` to
inject corruption and verify that only intact packets are delivered.

The link transport tests are the most useful reference when you are changing
connection or threading behavior. They build loopback transports from in-memory
pipes, run dedicated receive threads on both sides, and then exercise scenarios
such as buffered transfer, bidirectional streaming, reconnects, and blocking
behavior under constrained buffers.

The messaging tests show two particularly useful patterns. First, they treat end
of connection as a first-class state transition and verify that waiting senders
or receivers unblock cleanly. Second, they exercise multiplexing through real
message dispatch rather than through isolated helper tests.

Many examples also contain `ROO_TESTING` sections. Those sections let an
Arduino-style example run on Linux with fake UARTs, ring pipes, and fake board
support. If you add a new example that demonstrates important transport
behavior, it is often worth adding a `ROO_TESTING` section so the example can be
executed or inspected in host-side tests.

### Extending roo_transport

The cleanest way to extend `roo_transport` is to start at the lowest layer your
new medium naturally supports and then reuse everything above it.

If your medium already has packet boundaries, implement `PacketSender` for
outgoing packets and pair it with whatever receive path your medium exposes.
When packets arrive, call `LinkTransport::processIncomingPacket()` if you need a
reliable byte stream on top.

If your medium is an (unreliable) byte stream, wrap the stream in
`PacketSenderOverStream` and `PacketReceiverOverStream`, then build a
`LinkTransport` on top of that. That is exactly the pattern used by the UART-
based adapters.

Once you have a working `LinkTransport`, the rest of the stack composes
naturally:

- use `Link` or `LinkStream` directly if your application wants a stream,
- add `LinkMessaging` if your application wants message boundaries,
- add RPC if your application wants typed request/response calls.

If you want a new board-specific convenience adapter, follow the shape of the
existing Arduino adapters. Keep the adapter thin. Its job should be to connect a
platform's serial or stream API to the generic packet / link layers, not to
reimplement reliability, messaging, or RPC logic.

One useful design question is whether your new medium should surface resets as a
hard disconnect or try to hide them. In most cases, the right answer is to
surface them. The rest of the stack already knows how to rebuild higher-level
state from clean reconnect boundaries, and trying to fake continuity usually
makes failures harder to reason about.

### Limits and non-goals

The current important limits and constraints are:

- Packet payloads are capped at 250 bytes.
- RPC is unary only; streaming RPC is not part of the current public API.
- Reconnects are real session boundaries. In-flight stream, message, and RPC
  state is not preserved across a reset.
- A `Messaging` transport has a single active receiver. Sharing one physical
  link between multiple logical subsystems should be done through
  multiplexing.
- Throughput and latency depend strongly on UART configuration, buffering, and
  task scheduling.

The important non-goals are just as significant.

- This is not a TCP/IP stack.
- It is not a socket compatibility layer.
- It is not a routed or multi-hop network protocol.
- It does not try to hide the existence of peer resets or cable disconnects.

None of these constraints are accidental omissions. They are the tradeoffs that
keep the implementation compact, understandable, and efficient on the hardware
it is meant to serve.

## Conclusion

`roo_transport` is easiest to understand as a ladder. Packets add framing and
integrity. Links add ordered reliable streaming. Messaging adds explicit
message boundaries and reconnect-aware delivery. RPC adds typed service calls.
You do not need to adopt every layer. Start at the layer that matches your
application and only go lower when you need more control.

For application work, the examples under `examples/` are still the best next
step. For integration work, the main public entry points live under `src/`:
start with [roo_transport.h](../src/roo_transport.h), then move to the specific
headers for packets, links, messaging, or RPC as needed.
