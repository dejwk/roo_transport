# Stop Maintaining Your Own UART Protocol

Suppose you have a main MCU talking to a motor-control board, a display board,
or a sensor coprocessor over UART.

The first version is always tempting: add a header byte, a length field, a
checksum, and call it a protocol. On the bench, with short jumper wires, it can
look finished.

Then the real system arrives:

- a dropped byte shifts the parser until the next sync marker;
- a lost reply leaves the caller unsure whether to retry or wait longer;
- a peer reboot invalidates every in-flight exchange;
- every new message type reopens timeout and recovery logic you thought was
  already done.

At that point, the problem is no longer "how do I send bytes over UART?" The
problem is that your application has quietly inherited responsibility for a
transport layer.

## Why this problem is not trivial to fix

The usual fixes are individually simple and collectively expensive.

You add a delimiter so the receiver can recover boundaries. Then you add a
checksum because boundaries are not enough. Then a timeout because the checksum
does not help with lost data. Then sequence numbers because retries can create
duplicates. Then reset handling because retries across reboots are ambiguous.

None of that logic belongs to your domain model, but all of it becomes part of
your correctness story. That is why these UART protocols tend to become some of
the most brittle code in an embedded project.

## The roo_transport-based fix

If your payload format already exists and your application already thinks in
bytes, the right fix is usually not to redesign the whole protocol. It is to
put a real reliable stream underneath it.

That is the job of the `roo_transport` reliable link layer. It gives you a
reliable, ordered byte stream on top of an unreliable underlying link and takes
over the transport work: packet framing, acknowledgements, retransmissions,
connection setup, and flow control.

## Why the link layer is a good fit

Use the reliable link when your application already thinks in streams:

- tunneling an existing binary protocol;
- exposing a stream-like service from a coprocessor;
- moving a custom protocol from a short lab cable to a noisier real-world
  wiring setup;
- replacing ad hoc UART reliability code without redesigning payloads.

On Arduino targets, the easiest entry point is `ReliableSerial1` or
`ReliableSerial2`. They wrap a UART transport and expose a connected
`LinkStream`.

The basic shape is simple:

```cpp
#include "roo_io/data/input_stream_reader.h"
#include "roo_io/data/output_stream_writer.h"
#include "roo_transport.h"
#include "roo_transport/link/arduino/reliable_serial.h"

using namespace roo_transport;

ReliableSerial1 reliable_serial;

void setup() {
  Serial1.begin(5000000, SERIAL_8N1, kRxPin, kTxPin);

  reliable_serial.begin();
  LinkStream link = reliable_serial.connectOrDie();

  roo_io::OutputStreamWriter out(link.out());
  roo_io::InputStreamReader in(link.in());

  out.writeBeU32(123);
  out.writeString("Hello, peer!\n");
  out.flush();

  uint32_t reply_id = in.readBeU32();
  std::string reply = in.readString();
}
```

That is the adoption story: you keep your byte-oriented code model, but you stop
owning the low-level reliability machinery.

## What happens when the peer resets

This is where `roo_transport` is much more honest than typical "reliable UART"
claims.

If the peer resets, the current connection is broken. The library does not
pretend that an old stream can continue forever through a reboot. Instead, it
surfaces the boundary explicitly.

That gives you two sane policies:

- use `connectOrDie()` when the other board is essential and a reset should
  reboot the whole appliance;
- use `connect()` or `connectAsync()` when your application is expected to
  recover from peer restarts.

This is a good contract for embedded systems. You get transport reliability,
but you still make the application-level decision about what a reconnect means.

## Performance is not the reason to avoid it

The library's benchmark example exists for a reason. Reliable transport is only
useful if it is still fast enough to keep out of the way.

On ESP32, the current benchmark reports about **3.4 Mbps** over short wires at
5 Mbps UART, with sub-millisecond round-trip latency. The documentation also
reports about **2.5 Mbps** with **0.92 ms p99 round-trip latency** over a
**7.5 m unshielded four-wire cable** under heavy packet loss.

That is enough headroom for a large class of board-to-board protocols. In many
projects, the real bottleneck is no longer the transport overhead. It is the
application logic on either side.

## Where to start

If this is your use case, start with the
[ReliableSerial loopback example](../examples/ReliableSerial/Loopback/Loopback.ino).
It is the cleanest demonstration of the link layer by itself.

Then read the reliable-link section of the
[programming guide](programming_guide.md).

If you find yourself adding delimiters or length prefixes above the link, that
is the sign you should move one layer up to
[reset-tolerant messaging](reconnect_aware_messaging.md).