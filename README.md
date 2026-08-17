Did you ever wish that UART (Serial) was reliable, so that you could use it to connect two microcontrollers together at fast speeds and have them communicate as reliably as if they were using a proper network socket? Now you can!

This library provides a lightweight implementation of a communication protocol that provides reliable bidirectional streaming byte transfer over unreliable connection (such as UART).
On ESP32, with short wires, it achieves 3.4 Mbps throughput (85% of the theoretical maximum UART bandwidth supported), and sub-millisecond round-trip time latency, making it suitable for RPC (remote procedure calls). I also tested it over 7.5 meters of a cheap, unshielded, 4-wire cable, and it still pulled 2.5 Mbps and 0.92ms p99 rtt latency, despite experiencing ~20% packet loss in that setup.

Additionally, the library provides a more foundational packet transport, which provides UDP-like (or, ESP-NOW-like) functionality: transmission of small packets, with the possibility of packet loss, but ensuring integrity of packets that get transmitted.

Finally, the library supplies a functional RPC framework that works on top of this reliable messaging.

See the included examples.

For a guided walkthrough of the layers, usage patterns, and extension points,
see [the programming guide](doc/programming_guide.md).

## Host emulation

Host builds support both Arduino and ESP-IDF through roo_testing 2.0. With
Bazelisk 1.21 or newer, a plain command defaults to Arduino and prints a notice:

    bazel test ...
    bazel test ... --config=roo_testing_arduino_esp32
    bazel test ... --config=roo_testing_idf_esp32
    .roo_testing/bin/test_all_profiles ...

The files under .roo_testing are vendored from roo_testing; follow their
canonical-source headers when refreshing them.
