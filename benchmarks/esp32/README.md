# ESP32 UART throughput and hardware validation

The ESP32 reliable UART transport supports about **3.82 Mbit/s of useful payload**
with 5 Mbaud UARTs, 2 KiB protocol windows and priority-1 application/sender tasks
on the tested single-board loopback. The [benchmark example](../../examples/ReliableSerial/Benchmark/README.md)
shows the application setup; the diagnostic firmware in this directory exposes
UART and scheduling controls for measurements.

## Hardware and configuration

The measured board is an ESP32-D0WD-V3 revision 3.1 with a 240 MHz CPU, 80 MHz
APB clock and 4 MiB flash. The toolchain is Arduino-ESP32 3.3.7, ESP-IDF
v5.5.2-729-g87912cd291 and pioarduino platform 55.03.37. Both endpoints run on
this board, with no Wi-Fi/BLE traffic or unrelated application work.

Cross UART1 TX **GPIO4 → GPIO16** UART2 RX, and UART2 TX **GPIO17 → GPIO2**
UART1 RX. Use short jumper wires. The programming/console USB link is separate
from these two UART connections.

| Setting | Application example profile |
| --- | ---: |
| Baud and framing | 5,000,000; 8N1 |
| Protocol window per direction | 2 KiB |
| RX / TX software ring per UART | 256 / 1024 bytes |
| RX FIFO threshold / timeout | 64 bytes / 1 symbol |
| Application / sender priority | 1 / 1 |
| UART event priority | 24 on this SDK |
| Application chunk | 256 bytes |

`ReliableSerial1/2` use the native ESP-IDF UART stream specialization. Initialize
both UART drivers serially in `setup()` so their interrupts belong to that core,
then start application workers. The example uses Arduino main's core 1.

## Measured throughput

Rates count delivered, pattern-validated payload bytes. The standalone example
runs a latency batch before each bulk transfer. The diagnostic harness runs its
latency batch once and maintains progress/watchdog counters during transfers;
its rates describe a different workload.

| Workload and configuration | Samples | Useful Mbit/s |
| --- | ---: | ---: |
| Standalone, profile above | 10 × 1 MiB | **3.813356–3.827941**, mean **3.818552** |
| Standalone, RX ring 512 bytes | 3 × 1 MiB | 3.822422–3.832103 |
| Standalone, TX ring 576 bytes | 3 × 1 MiB | 3.802650–3.807706 |
| Standalone, FIFO threshold 96 bytes | 3 × 1 MiB | 3.777939–3.785972 |
| Standalone, protocol window 1 KiB | 3 × 1 MiB | 3.648584–3.662914 |
| Diagnostic, profile above | 3 × 1 MiB | 3.687678–3.694374, mean 3.690453 |
| Diagnostic, forced 50 ms receive break | 2 × 1 MiB | 3.578510–3.683906; complete recovery |

With the example profile, ten batches of 100 one-byte round trips report
median latencies of 0.971–1.006 ms and p99 latencies of 1.101–1.463 ms per batch.
These latency measurements use the same single-board wiring and workload.

Each changed setting in the table is measured separately; the other settings
remain as listed above. These results support tested combinations, not freely
interchangeable buffer minima. Ring sizes and windows are per endpoint;
metadata, SDK queues and thread stacks consume additional RAM.

[Measurement records and console transcripts](results/2026-11-06-minimum.json)
contain the complete configurations and firmware/source fingerprints. These
measurements cover one classic ESP32 running both endpoints. Two-board links,
long cables, different message sizes and radio/application load need their own
validation.

The classic ESP32's 5 Mbaud ceiling yields 4 Mbit/s of raw bytes in 8N1. A full
transport frame carries 248 useful bytes in 256 wire bytes, giving an ideal
payload ceiling of `5,000,000 × 8/10 × 248/256 = 3,875,000 bit/s`, before gaps and
control work.

## Buffering and scheduling

At 5 Mbaud, the 128-byte RX FIFO receives a byte every 2 µs. A threshold of
120 leaves nominal headroom of only 16 µs; a threshold of 64 leaves 128 µs for
interrupt service. The interrupt comparator and service latency consume some of
that margin. Software rings cannot recover bytes lost to a hardware FIFO
overflow, and increasing the threshold to reduce interrupt frequency trades
away service margin.

Buffered TX lets the driver maintain wire traffic while application tasks run.
Larger rings do not guarantee higher throughput: queueing, partial-read costs,
interrupt ownership and retransmission timing interact. Keep enough protocol
window space for data in flight while acknowledgments return.

Keep producers and consumers able to fill/drain useful batches alongside their
sender workers. The example uses priority 1 for all three. To configure a sender,
pass complete attributes, including its stack budget:

```cpp
roo::thread::attributes sender;
sender.set_name("serial1-send");
sender.set_stack_size(4096);
sender.set_priority(1);
reliable_serial.begin(sender);
```

`begin(attributes)` copies the attributes and does not merge them with defaults.
It configures only the sender; UART event task scheduling is separate. The
no-argument `begin()` uses sender priority 23 and a 4096-byte stack on this SDK.

On receive and error notifications, the transport snapshots the readable UART
backlog and drains that byte budget, including chunks that contain no complete
valid packet. A read that makes no progress ends the drain. The bounded budget
lets subsequent callbacks service fresh data without monopolizing the event task.

## Build

Keep the Roo repositories as sibling source checkouts. The builder copies their
sources into a local project and records the configuration, source fingerprints
and binary hashes in `manifest.json`:

```sh
python3 benchmarks/esp32/build.py \
  --platform /path/to/installed/espressif32 --board esp32dev
```

The output defaults to `~/.cache/roo_transport/esp32-benchmark/firmware`. Builds
use 240 MHz, disabled exceptions and a 4096-byte Arduino UART event stack. The
helper uses `deep` dependency discovery to handle SDK macros in compiler response
files. Build options include `--cpu-mhz`, `--uart-event-core -1/0/1` and
`--uart-event-priority N`, where supported by the core. `--transport-ref REF`
selects transport sources without changing the checkout; an explicit sender
priority requires that selected source to support `begin(attributes)`.

Building does not upload. Preserve any firmware you need before installing the
benchmark, and flash its matching bootloader, partition and application images.
The generated project uses 115200 upload speed for USB-UART bridge compatibility.

For the standalone teaching sketch, pass
`--sketch examples/ReliableSerial/Benchmark/Benchmark.ino` and a separate output
directory. That sketch starts after reset/console readiness and uses editable
constants rather than the diagnostic command protocol below.

## Run

Install pyserial and use the detected console port (`COM...` on Windows):

```sh
python3 benchmarks/esp32/run.py --port /dev/ttyUSB0 --reset \
  --baud 5000000 --samples 3 --application-priority 1 --sender-priority 1 \
  --rx-buffer 256 --tx-buffer 1024 --window-log 3 --rx-fifo 64 --rx-timeout 1 \
  --output results/reliable.json \
  --manifest ~/.cache/roo_transport/esp32-benchmark/firmware/manifest.json

python3 benchmarks/esp32/run.py --port /dev/ttyUSB0 --reset \
  --mode raw-idf --chunk 4096 --samples 3 --output results/raw-idf.json
```

`--reset` pulses RTS/EN with GPIO0 released and retains the serial connection to
capture `READY`. A software monitor reboots stalled firmware after twenty seconds
without delivered progress. `--timeout` bounds the host's wait. JSON results and
the console transcript are saved together; `--manifest` attaches build evidence.

## Diagnostic parameters

The console accepts:

```text
run BAUD RX_BUFFER TX_BUFFER WINDOW_LOG MODE BYTES SAMPLES APP_PRIORITY RX_FIFO RX_TIMEOUT CHUNK [SPLIT_IRQ [FAULT_US [SENDER_PRIORITY]]]
```

- `MODE=0` / `--mode reliable`: `ReliableSerial1/2` with native IDF streams.
- `MODE=1` / `raw-arduino`: `HardwareSerial.write/readBytes`.
- `MODE=2` / `raw-idf`: `uart_write_bytes/uart_read_bytes`; Arduino installs the
  driver and routes pins. `MODE=3` / `raw-idf-reverse` tests GPIO17 → GPIO2.
- `WINDOW_LOG=3/4/5/6` selects nominal 2/4/8/16 KiB windows. `BYTES` and `SAMPLES`
  set the payload count and repetitions (`--bytes`, `--samples`).
- `APP_PRIORITY=0` preserves the Arduino main priority and a priority-1 producer;
  other values set both application tasks. `SENDER_PRIORITY=0` uses the library
  default, not priority 0. Nonzero sender priorities require reliable mode.
- RX rings must be at least 129 bytes. `TX_BUFFER=0` selects unbuffered TX;
  positive values size its ring. Buffer setter results are checked.
- `RX_FIFO` selects a threshold of 1–127 bytes. `RX_TIMEOUT` is 0–101 symbol
  units; zero disables timeout events and can stall control packets below the
  FIFO threshold. Threshold/timeout setter results are checked.
- `CHUNK` is a positive multiple of 256, up to 4096, for consistent pattern phase.
- `SPLIT_IRQ=0` installs both UARTs on the application core; `--split-irq` installs
  UART1 on core 0 and UART2 on the application core. `CONFIG` reports IRQ cores.
- `--fault-us 50000` forces UART2 RX internally low after 32768 delivered bytes,
  then restores its input route. It does not change the electrical drive on
  either wire. Fault injection requires reliable mode and at least 65536 bytes.

## Validation and limits

The runner rejects incomplete data, mismatched settings, wrong sample numbers,
corrupt bytes and observed raw UART errors. Reliable runs require response EOF
and worker shutdown. Fault runs additionally require `FAULT fired=1 restored=1`.
The tested 50 ms receive break recovers with complete payload delivery.

Startup events settle before timing; `STARTUP_ERRORS` retains their classification
separately from transfer errors. Raw modes observe break, buffer-full,
FIFO-overflow, framing and parity callbacks. Reliable mode reports `observed=0`
because the production transport owns those callbacks; independent hardware-error
counts are unavailable in that mode. Payload and lifecycle checks still apply.
Packet counter deltas can straddle sample boundaries and are not exact wire-loss
estimates.

Primary references: [ESP-IDF UART documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/peripherals/uart.html),
[ESP32 capability limits](https://github.com/espressif/esp-idf/blob/v5.5.2/components/soc/esp32/include/soc/soc_caps.h)
and [Arduino-ESP32 Serial API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html).
