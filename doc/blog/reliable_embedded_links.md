# roo_transport by use case

The most useful way to evaluate `roo_transport` is not to start from its layer
diagram. Start from the communication problem you already have.

If one board is talking to another over UART, SPI-backed byte pipes, or another
simple point-to-point medium, the real question is usually one of these:

- how do I stop maintaining my own framing and retry protocol?
- how do I exchange commands cleanly when boards can reset independently?
- how do I make a coprocessor feel like a service instead of an opcode parser?
- how do I stream telemetry when occasional loss is acceptable?

This short series is organized around those use cases.

## Pick the post that matches your system

- [Stop Maintaining Your Own UART Protocol](reliable_uart_streams.md)
  If you already have a byte-oriented binary protocol and want a reliable
  stream underneath it.
- [Reset-Tolerant Command Channels Between Boards](reconnect_aware_messaging.md)
  If your application works in discrete commands, events, and state updates.
- [Treat a Coprocessor Like a Service](rpc_for_coprocessors.md)
  If the remote board really looks like an API with calls such as
  `readConfig()`, `setBrightness()`, or `runCalibration()`.
- [When Losing a Sample Is Fine](packet_telemetry.md)
  If you need intact packets for telemetry or sensor updates but do not need
  every single sample delivered.

## Why the series is organized this way

`roo_transport` does have layers: packets, reliable links, messaging, and RPC.
That structure is important for implementation, but adoption usually does not
start from there. Most projects begin with an application shape that already
exists.

- If you already think in bytes, start at the reliable link.
- If you already think in whole messages, start at messaging.
- If the remote side is a service, start at RPC.
- If the newest sample matters more than every sample, packets may be enough.

That is one of the library's main strengths: you do not have to adopt the whole
stack at once.

## Read this after the blog posts

When you are ready for the full API surface, extension points, and lifecycle
details, the reference document is the
[programming guide](programming_guide.md).

If you prefer to jump directly into runnable code, the most relevant examples
in the repository are:

- [Packets](../examples/Packets/Packets.ino)
- [ReliableSerial loopback](../examples/ReliableSerial/Loopback/Loopback.ino)
- [Messaging simple](../examples/Messaging/Simple/Simple.ino)
- [Messaging multiplexing](../examples/Messaging/Multiplexing/Multiplexing.ino)
- [RPC simple](../examples/Rpc/Simple/Simple.ino)
- [RPC serialization](../examples/Rpc/Serialization/Serialization.ino)
- [RPC asynchronous](../examples/Rpc/Asynchronous/Asynchronous.ino)
