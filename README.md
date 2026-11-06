# roo_transport

roo_transport delivers **reliable UART communication at multi-megabit speeds**,
with bidirectional byte streams, framed messaging and RPC for microcontrollers.
A reliable link handles packet framing, integrity checks, acknowledgments and
retransmission so applications can exchange an ordered byte stream through roo_io
readers and writers.

The library also provides packet transports for applications that need datagram
semantics: each delivered packet has its integrity checked, while packet delivery
is not guaranteed.

See the [programming guide](doc/programming_guide.md) for the layers, APIs and
extension points. The [Loopback example](examples/ReliableSerial/Loopback/Loopback.ino)
shows structured messages, and the [benchmark example](examples/ReliableSerial/Benchmark/README.md)
shows UART setup, latency measurement and bulk transfer.

## Performance

Get about **3.82 Mbit/s of useful payload on ESP32** and up to **4.74 Mbit/s
on RP2040**, at normal CPU clocks. These are delivered and validated application
bytes, with the link handling integrity checks, flow control and retransmission.

On classic ESP32, that uses **95.5% of the UART's theoretical raw payload
bandwidth**: 5 Mbaud with 8N1 framing carries at most 4 Mbit/s of byte data.
Accounting for protocol framing, the ideal full-frame payload ceiling is
3.875 Mbit/s; the measured throughput reaches **98.5% of that ceiling**.

Measured single-board loopback configurations include:

| Platform | UART baud | Window per direction | Application/sender priority | Payload Mbit/s |
| --- | ---: | ---: | ---: | ---: |
| Classic ESP32, Arduino UART backend | 5,000,000 | 2 KiB | 1/1 | 3.81–3.83 |
| RP2040, native DMA backend | 8,312,500 | 8 KiB | 1/1 | 4.33–4.47 |
| RP2040, native DMA backend | 8,312,500 | 16 KiB | 6/6 | 4.65–4.74 |

These rates count delivered and validated payload bytes. UART framing and
protocol overhead use additional bandwidth. The ESP32 and Pico guides describe
the complete [ESP32 configuration](benchmarks/esp32/README.md) and
[Pico configuration](benchmarks/pico/README.md), including buffers, clocks, wiring,
workloads and measurement records. Actual performance depends on these settings
and the application's scheduling and traffic.

Two-board ESP32 measurements include about 3.4 Mbit/s with short wires and
2.5 Mbit/s over 7.5 meters of inexpensive, unshielded cable. Cable length and
signal quality affect retransmission overhead and latency.

The benchmark example selects native DMA automatically on supported RP2040
Arduino-Pico FreeRTOS builds. Its mode, workload and platform settings are
editable constants in the sketch.

## Host emulation

Host builds support Arduino and ESP-IDF through roo_testing 2.0. With
Bazelisk 1.21 or newer, a plain command defaults to Arduino and prints a notice:

```sh
bazel test ...
bazel test ... --config=asan
bazel test ... --config=roo_testing_arduino_esp32
bazel test ... --config=roo_testing_idf_esp32
.roo_testing/bin/test_all_profiles ...
```

Run an Arduino example on the host and stop it with Ctrl-C:

```sh
bazel run //examples/Packets:Packets
```

The files under `.roo_testing` are vendored from roo_testing; follow their
canonical-source headers when refreshing them.
