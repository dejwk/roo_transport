# When Losing a Sample Is Fine

Suppose one board publishes temperature readings, knob positions, battery
status, or periodic health beacons to another board.

In that kind of stream, the newest sample usually matters more than every
intermediate one. If a single reading is missed, the system is still fine as
long as the next one arrives intact.

That sounds like a case for "just send bytes," but it usually is not. Raw bytes
still leave you with framing, corruption detection, and recovery when a noisy
link destroys packet boundaries.

## Why this problem is not trivial to fix

Many telemetry streams do not need TCP-like behavior, but they do need clean
packet semantics.

Examples:

- a temperature reading once per second;
- joystick or knob position updates;
- periodic status beacons;
- sampled measurements where the latest value matters more than every
  intermediate value.

In those cases, full reliable transport can be the wrong abstraction. It adds
state and buffering you may not need.

What you do still need is integrity. A corrupted packet should never be accepted
as if it were valid data.

That is the subtle part of the problem: you want data integrity without paying
for full end-to-end delivery guarantees.

## The roo_transport-based fix

This is exactly what the `roo_transport` packet layer is for.

## What the packet layer guarantees

The packet layer gives you a clear, lightweight contract:

- delivered packets are intact;
- corrupted packets are dropped;
- packets may be lost;
- packets may arrive out of order.

For byte-stream transports such as UART, `PacketSenderOverStream` and
`PacketReceiverOverStream` add framing plus integrity checking so packet
boundaries can be recovered even when bytes are lost or corrupted.

The practical payload limit is **250 bytes per packet**, which is enough for a
large class of sampled telemetry and control data.

## Why this is often the right starting point

This layer is a good fit when:

- you are sending periodic sensor updates;
- stale data is not worth retransmitting;
- you want the receiver to stay in sync even if the wire is noisy;
- you are optimizing for simplicity and low overhead.

The example in the repository models exactly that shape: a simple temperature
sensor publishes one reading per second.

The sender constructs one packet per reading:

```cpp
roo::byte buf[8];
roo_io::MemoryOutputIterator itr(buf, buf + 8);
roo_io::WriteBeU32(itr, num_reading);
roo_io::WriteBeS32(itr, temperature);
sender.send(buf, itr.ptr() - buf);
```

And the receiver decodes whole packets instead of trying to recover record
boundaries from a raw byte stream.

## What happens when the wire is interrupted

The packet example has a great real-world property: if you unplug the wire, the
client stops receiving updates. When the wire comes back, it receives future
packets again. The packets that do arrive are intact.

That is often exactly what you want for telemetry. There is no fake continuity,
no replay of stale samples, and no need to carry the state required for a fully
reliable stream.

## When not to use this layer

Do not start here if your application really needs:

- every command delivered;
- ordered request/response behavior;
- explicit reconnect-aware conversations;
- a service-style API.

Those are the cases for higher layers.

- move to [reliable streams](reliable_uart_streams.md) if you already have a
  byte-oriented protocol and need delivery guarantees;
- move to [messaging](reconnect_aware_messaging.md) if you need whole commands
  or records delivered reliably;
- move to [RPC](rpc_for_coprocessors.md) if the remote side is really a
  service.

## Where to start

Start with the [Packets example](../examples/Packets/Packets.ino).
It is a realistic demonstration of the intended use case: lightweight,
integrity-checked telemetry where occasional packet loss is acceptable.