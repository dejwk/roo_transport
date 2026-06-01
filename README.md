Did you ever wish that UART (Serial) was reliable, so that you could use it to connect two microcontrollers together at fast speeds and have them communicate as reliably as if they were using a proper network socket? Now you can!

This library provides a lightweight implementation of a communication protocol that provides reliable bidirectional streaming byte transfer over unreliable connection (such as UART).
On ESP32, with short wires, it achieves 3.4 Mbps throughput (85% of the theoretical maximum UART bandwidth supported), and sub-millisecond round-trip time latency, making it suitable for RPC (remote procedure calls). I also tested it over 7.5 meters of a cheap, unshielded, 4-wire cable, and it still pulled 2.5 Mbps and 0.92ms p99 rtt latency, despite experiencing ~20% packet loss in that setup.

Additionally, the library provides a more foundational packet transport, which provides UDP-like (or, ESP-NOW-like) functionality: transmission of small packets, with the possibility of packet loss, but ensuring integrity of packets that get transmitted.

Finally, the library supplies a functional RPC framework that works on top of this reliable messaging.

See the included examples.

For a use-case-driven introduction, start with
[roo_transport by use case](doc/reliable_embedded_links.md).

If you already know your problem shape, jump directly to:

- [Stop Maintaining Your Own UART Protocol](doc/blog/reliable_uart_streams.md)
- [Reset-Tolerant Command Channels Between Boards](doc/blog/reconnect_aware_messaging.md)
- [Treat a Coprocessor Like a Service](doc/blog/rpc_for_coprocessors.md)
- [When Losing a Sample Is Fine](doc/blog/packet_telemetry.md)

For a guided walkthrough of the layers, usage patterns, and extension points,
see [the programming guide](doc/programming_guide.md).
