Did you ever wish that UART (Serial) was reliable, so that you could use it to connect two microcontrollers together at fast speeds and have them communicate as reliably as if they were using a proper network socket? Now you can!

This library provides a lightweight implementation of a communication protocol that provides reliable bidirectional streaming byte transfer over unreliable connection (such as UART).
On ESP32, it achieves 3.4 Mbps throughput (85% of the theoretical maximum UART bandwidth supported), and sub-millisecond round-trip time latency, making it suitable for
RPC (remote procedure calls).

Additionally, the library provides a more foundational packet transport, which provides UDP-like (or, ESP-NOW-like) functionality: transmission of small packets, with the possibility
of packet loss, but ensuring integrity of packets that get transmitted.

See the included examples.
