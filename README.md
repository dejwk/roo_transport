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

Get about **3.83 Mbit/s of useful payload between two ESP32s** and up to **4.74 Mbit/s
on RP2040**, at normal CPU clocks. These are delivered and validated application
bytes, with the link handling integrity checks, flow control and retransmission.

On classic ESP32, the two-board mean uses **95.8% of the UART's theoretical
raw payload bandwidth**: 5 Mbaud with 8N1 framing carries at most 4 Mbit/s of byte data.
Accounting for protocol framing, the ideal full-frame payload ceiling is
3.875 Mbit/s; the measured throughput reaches **98.9% of that ceiling**.

Each reliable UART data frame adds **8 bytes** to up to 248 bytes of application
payload. That includes a 2-byte transport header, a 4-byte integrity check and
2 bytes of COBS framing. For the same payload size, uncompressed TCP adds at
least 40 bytes of TCP/IPv4 headers or 60 bytes of TCP/IPv6 headers before
link-layer framing. Acknowledgments and retransmissions use additional
bandwidth. [TCP/IP header sizes (RFC 6691)](https://www.rfc-editor.org/rfc/rfc6691.html#section-2)

Measured short-wire configurations include:

| Platform | Topology | UART baud | Window per direction | Application/sender priority | Payload Mbit/s |
| --- | --- | ---: | ---: | ---: | ---: |
| Classic ESP32, Arduino UART backend | Two boards | 5,000,000 | 2 KiB | 1/1 | **3.83** |
| Classic ESP32, Arduino UART backend | One-board loopback | 5,000,000 | 2 KiB | 1/1 | 3.81–3.83 |
| RP2040, native DMA backend | One-board loopback | 8,312,500 | 8 KiB | 1/1 | 4.33–4.47 |
| RP2040, native DMA backend | One-board loopback | 8,312,500 | 16 KiB | 6/6 | 4.65–4.74 |

These rates count delivered and validated payload bytes. UART framing and
protocol overhead use additional bandwidth. The ESP32 and Pico guides describe
the complete [ESP32 configuration](benchmarks/esp32/README.md) and
[Pico configuration](benchmarks/pico/README.md), including buffers, clocks, wiring,
workloads and measurement records. Actual performance depends on these settings
and the application's scheduling and traffic.

The two-board ESP32 profile sustains 30 checked 1 MiB transfers across repeated
boots and both payload directions, with **0.553–0.560 ms median round-trip
latency per batch**. A separate 7.5-meter unshielded-cable measurement delivers
about 2.5 Mbit/s. Cable length and signal quality affect retransmission overhead
and latency.

The benchmark example selects native DMA automatically on supported RP2040
Arduino-Pico FreeRTOS builds. Its mode, workload and platform settings are
editable constants in the sketch.

## RAM footprint

On classic ESP32 with the Arduino UART backend, the estimated footprint is
**15.2 KiB of additional heap per endpoint**. This includes the UART driver,
the benchmark's protocol windows and the default worker stacks. Application
buffers are separate. See the [ESP32 RAM breakdown](benchmarks/esp32/README.md#ram-footprint).

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
