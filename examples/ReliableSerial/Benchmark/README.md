# Reliable serial benchmark

This sketch demonstrates a reliable byte stream over UART, then measures its
round-trip latency and useful payload throughput. Read the platform setup first;
the server, client and measurement code below it is shared by ESP32 and Pico.

## Wire the endpoints

The default `kMode = Mode::kLoopback` runs a server and client on one board, using
its two hardware UARTs. Connect these pins with jumper wires:

| Board | Server TX → client RX | Client TX → server RX |
| --- | --- | --- |
| ESP32 | GPIO4 → GPIO16 | GPIO17 → GPIO2 |
| Pico | GP12 → GP5 | GP4 → GP13 |

For two boards, edit `kMode` to `Mode::kServer` on one and `Mode::kClient` on the
other. Connect server TX to client RX, client TX to server RX, and their grounds
together. Use compatible signal levels and the same baud rate on both boards.
Only the UART used by that board's role is initialized.

## Edit the constants

Settings are ordinary `static constexpr` values in `Benchmark.ino`; edit them
where they are used rather than passing preprocessor overrides:

- `kMode` selects loopback, server or client.
- `kRuns` is zero for continuous client operation; a positive value ends after
  that many latency/throughput cycles. The server ends when requests reach EOF.
- `kLatencySamples` is the number of one-byte round trips measured per cycle.
- `kPayloadBytes` is the number of bulk response bytes per cycle.
- `kChunkSize` controls the blocks used to write and validate the repeating data.
- `kIoPriority` sets the application priority and, on ESP32/native DMA, the
  transport sender priority.
- `kWaitForSerial` waits for a console before starting. Set it to `false` for a
  headless peer, especially a Pico server with no USB serial monitor open.

The platform sections keep their pin, baud and buffer choices together:

| Platform | Backend | Baud | Protocol window per direction |
| --- | --- | ---: | ---: |
| ESP32 Arduino | `ReliableSerial1/2` | 5,000,000 | 2 KiB |
| Native RP2040 Arduino-Pico FreeRTOS | `pico::ReliableUartDma` | 8,312,500 | 8 KiB |
| Other Arduino-Pico builds/emulation | Arduino serial adapter | 115,200 | 4 KiB |

The native DMA branch is selected automatically for RP2040 FreeRTOS hardware.
RP2350 and host emulation use the Arduino adapter. This threaded example requires
FreeRTOS on native Arduino-Pico. The Bazel example uses an ESP32 fake board;
native DMA reception runs on hardware. At 115200 baud, the default 1 MiB response is
slow; reduce `kPayloadBytes` when exploring that fallback.

ESP32 uses a 256-byte RX ring, a 1024-byte TX ring and a 64-byte hardware RX FIFO
threshold. The FIFO margin gives the interrupt time to drain received bytes at
high baud rates. Both applications and senders run at priority 1; receive workers
retain their existing priorities. Matching the application and sender priorities
lets the producer feed the link without being continually preempted by its sender.

Pico DMA uses aligned 8 KiB RX rings and an 8 KiB TX ring per endpoint. Its setup
selects the system clock for `clk_peri` before configuring either UART, avoiding
the default peripheral clock's 3 Mbaud limit without changing the CPU clock.
Configure other peripherals for that clock policy when adapting this setup.
All active UARTs are configured before constructing DMA endpoints. Construction
starts reception; construction and shutdown stay on the same setup core.

## Follow the request and response

The client writes a variable-length integer requesting N bytes. The server reads
that length and returns exactly N bytes of a repeating pattern. It accepts the
next request after finishing the response. The loopback starts the server in a
separate thread because the client blocks while waiting for those bytes.

The latency test requests one byte, flushes the small request and reads the
response. The measured interval includes the complete round trip. `flush()` makes
a partial buffer available for transmission; it does not promise remote receipt.

The throughput test requests a larger response, reads exactly that many bytes,
and validates the pattern. Useful throughput is payload bits divided by elapsed
time; protocol overhead is not counted as payload. Printing happens afterward.
Try disconnecting a wire during a transfer: progress stalls, then resumes when
the link can deliver data again.

A finite client closes its request stream and waits for response EOF. Readers
and writers are destroyed before their `Link`, and the loopback joins its server
before stopping either transport. The Pico Arduino UART adapter leaves its
idle receive worker alive until reset because it cannot stop an idle worker.

## Build and explore

With sibling Roo checkouts and an installed PlatformIO platform, build this
standalone sketch using the hardware helper:

```sh
python3 benchmarks/esp32/build.py --platform /path/to/installed/espressif32 \
  --output ~/.cache/roo_transport/esp32-benchmark/example \
  --sketch examples/ReliableSerial/Benchmark/Benchmark.ino
```

Use `benchmarks/pico/build.py` with an Arduino-Pico-capable platform for Pico.
Building does not upload. When building ESP32 outside the helper, also set the
Arduino core build flag `-DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=4096`; the core's
UART event task has a separate stack from the transport sender.

For tested configurations and measurement records, see the [ESP32 guide](../../../benchmarks/esp32/README.md)
and [Pico guide](../../../benchmarks/pico/README.md). The adjacent
[Loopback example](../Loopback/Loopback.ino) illustrates paced structured messages
rather than bulk transfer measurements.
