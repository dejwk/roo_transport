# ESP32 UART throughput and hardware validation

Two ESP32s connected by short crossed UART wires sustain **3.83 Mbit/s of useful
payload**, with **0.553–0.560 ms median round-trip latency per batch**, using
5 Mbaud UARTs, 2 KiB protocol windows and priority-1 application/sender tasks.
The payload rate is **95.8% of raw UART byte bandwidth** and **98.9% of the ideal
full-frame protocol ceiling**.

The [benchmark example](../../examples/ReliableSerial/Benchmark/README.md) teaches
the request/response workload. The tools in this directory run reproducible
paired-board and single-board measurements without adding configuration macros
to that example.

## Hardware and configuration

The two-board setup uses an ESP32-D0WD-V3 revision 3.1 and an ESP32-D0WDQ6
revision 1.1. Each runs at 240 MHz with an 80 MHz APB clock. The toolchain is
Arduino-ESP32 3.3.7, ESP-IDF v5.5.2-729-g87912cd291 and pioarduino platform
55.03.37. There is no Wi-Fi/BLE traffic or unrelated application work.

Each board uses **GPIO4 as TX** and **GPIO2 as RX**. Cross GPIO4 on each board
to GPIO2 on the other, and connect their grounds. The programming/console USB
links are separate from the UART connection carrying benchmark data.

| Setting | Paired-board and single-board example profiles |
| --- | ---: |
| Baud and framing | 5,000,000; 8N1 |
| Protocol window per direction | 2 KiB |
| RX / TX software ring per UART | 256 / 1024 bytes |
| RX FIFO threshold / timeout | 64 bytes / 1 symbol |
| Application / sender priority | 1 / 1 |
| UART event priority | 24 on this SDK |
| Application chunk | 256 bytes |

`ReliableSerial1/2` use the native ESP-IDF UART stream specialization. Initialize
each active UART driver in `setup()` so its interrupt belongs to that core, then
start application workers. The example uses Arduino main's core 1.

For a **single-board loopback**, use the illustrative example's own wiring:
UART1 TX **GPIO4 → GPIO16** UART2 RX, and UART2 TX **GPIO17 → GPIO2** UART1 RX.
That setup places both endpoints on an ESP32-D0WD-V3 revision 3.1.

## Measured throughput

Rates count delivered, pattern-validated payload bytes. A latency batch of 100
one-byte requests precedes each bulk transfer in the paired-board workload.
The data direction in the table is server to client; requests and acknowledgments
travel in the reverse direction.

| Two-board payload direction | Samples | Useful Mbit/s range | Mean Mbit/s |
| --- | ---: | ---: | ---: |
| D0WD-V3 server → D0WDQ6 client | 20 × 1 MiB across two boots | 3.831351–3.834424 | 3.833180 |
| D0WDQ6 server → D0WD-V3 client | 10 × 1 MiB | 3.832305–3.834093 | 3.833128 |
| All measured transfers | 30 × 1 MiB | **3.831351–3.834424** | **3.833163** |

Every payload byte is checked. Both endpoints complete response EOF and worker
shutdown in each run. The aggregate rate, total payload bits divided by total
timed seconds, is also 3.833163 Mbit/s to six decimal places.

Across the 30 latency batches, reported medians range from **0.553 to 0.560 ms**
and reported p99 values from **0.722 to 1.630 ms**, including the first batch of
each run. These are ranges of per-batch statistics, not pooled percentiles.
[Paired-board measurement records](results/2026-11-06-two-board.json) contain both
firmware/source fingerprints, exact elapsed times, configurations and console
transcripts.

The two-board parameter screens keep the same baud rate and UART buffers:

| Window per direction | Application/sender priority | Chunk bytes | Samples | Mean Mbit/s |
| ---: | ---: | ---: | ---: | ---: |
| 2 KiB | 1/1 | 256 | 30 × 1 MiB | **3.833163** |
| 16 KiB | 1/1 | 256 | 3 × 1 MiB | 3.832759 |
| 16 KiB | 23/23 | 256 | 3 × 1 MiB | 3.828413 |
| 2 KiB | 23/23 | 256 | 3 × 1 MiB | 3.828821 |
| 2 KiB | 1/1 | 1024 | 3 × 1 MiB | 3.831011 |

These screens do not show a throughput gain from larger windows, higher
priorities or larger application blocks. The practical profile uses 2 KiB
windows and priority 1. All seven runs, totaling 42 MiB, complete byte checks,
EOF and shutdown on both boards; the settings tested do not establish a global
maximum for every workload.

Single-board measurements use a different CPU workload:

| One-board workload and configuration | Samples | Useful Mbit/s |
| --- | ---: | ---: |
| Standalone example, profile above | 10 × 1 MiB | 3.813356–3.827941, mean 3.818552 |
| Standalone, RX ring 512 bytes | 3 × 1 MiB | 3.822422–3.832103 |
| Standalone, TX ring 576 bytes | 3 × 1 MiB | 3.802650–3.807706 |
| Standalone, FIFO threshold 96 bytes | 3 × 1 MiB | 3.777939–3.785972 |
| Standalone, protocol window 1 KiB | 3 × 1 MiB | 3.648584–3.662914 |
| Diagnostic harness, profile above | 3 × 1 MiB | 3.687678–3.694374, mean 3.690453 |
| Diagnostic, forced 50 ms receive break | 2 × 1 MiB | 3.578510–3.683906; complete recovery |

The single-board example's ten latency batches report medians of 0.971–1.006 ms
and p99 values of 1.101–1.463 ms per batch. The diagnostic harness runs latency
once and maintains progress/watchdog counters during transfers; its rates
therefore describe a different workload. [Single-board measurement records](results/2026-11-06-minimum.json)
retain the complete configurations and validation results.

Each changed setting in the single-board table is measured separately; the
other settings remain as listed above. Results support tested combinations,
not freely interchangeable buffer minima. Rings and windows are per endpoint;
metadata, SDK queues and thread stacks consume additional RAM. Different cable
lengths, message sizes and radio/application load need their own measurements.

The classic ESP32's 5 Mbaud ceiling yields 4 Mbit/s of raw bytes in 8N1. A full
transport frame carries 248 useful bytes in 256 wire bytes, giving an ideal
payload ceiling of `5,000,000 × 8/10 × 248/256 = 3,875,000 bit/s`, before gaps and
control work. The paired-board mean uses 95.8% and 98.9% of those respective
ceilings.

## Reproduce the two-board benchmark

Keep the Roo repositories as sibling source checkouts. Build the paired firmware
with an installed Arduino-ESP32 PlatformIO platform:

```sh
python3 benchmarks/esp32/build_two_board.py \
  --platform /path/to/installed/espressif32 \
  --output ~/.cache/roo_transport/esp32-two-board/firmware --runs 10
```

The helper generates finite server/client copies of the illustrative sketch,
selects the role and GPIO4/GPIO2 wiring, and adds console start/completion records.
It builds each role sequentially into `server/` and `client/` projects. Each
project has a source/build manifest, and `pair-manifest.json` describes the pair.
The illustrative sketch itself stays unchanged.

Flash the server project onto one board and the client project onto the other.
**GPIO2 must be low or floating to enter the ROM downloader**. Hold the other
board in reset while flashing so its idle-high GPIO4 TX does not keep GPIO2
high. Normal application boot ignores GPIO2's level; see [ESP32 boot-mode
selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

Run both consoles together, using the detected ports (`COM...` on Windows):

```sh
python3 benchmarks/esp32/run_two_board.py \
  --server-port /dev/ttyUSB0 --client-port /dev/ttyUSB1 \
  --firmware ~/.cache/roo_transport/esp32-two-board/firmware \
  --reset --output results/two-board.json
```

The runner opens both consoles before resetting them, waits for both `READY`
records, and starts both endpoints. It accepts only matching settings, complete
payload timings and finite EOF/stop completion on **both** boards. JSON evidence
and separate `.server.log` / `.client.log` transcripts are saved together.

To measure the opposite payload direction, swap which board receives each role's
firmware and swap the console-port arguments; the crossed wiring stays the same.
Builder options include `--bytes`, `--samples`, `--baud`, `--window-log`, `--chunk`
and `--io-priority`. Here `--runs` counts bulk-transfer cycles, while
`--samples` counts one-byte latency requests per cycle. Offline tool checks run
with `python3 benchmarks/esp32/test_two_board_tools.py`.

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

## Single-board diagnostic harness

The diagnostic builder copies local Roo sources and records source/configuration
fingerprints and binary hashes in `manifest.json`:

```sh
python3 benchmarks/esp32/build.py \
  --platform /path/to/installed/espressif32 --board esp32dev
```

The output defaults to `~/.cache/roo_transport/esp32-benchmark/firmware`. Builds
use 240 MHz, disabled exceptions and a 4096-byte Arduino UART event stack. Options
include `--cpu-mhz`, `--uart-event-core -1/0/1` and `--uart-event-priority N`, where
supported by the core. `--transport-ref REF` selects transport sources without
changing the checkout; an explicit sender priority requires those sources to
support `begin(attributes)`. Building does not upload firmware.

For the standalone teaching sketch, pass
`--sketch examples/ReliableSerial/Benchmark/Benchmark.ino` and a separate output
directory. That sketch uses editable constants instead of the diagnostic command
protocol below.

```sh
python3 benchmarks/esp32/run.py --port /dev/ttyUSB0 --reset \
  --baud 5000000 --samples 3 --application-priority 1 --sender-priority 1 \
  --rx-buffer 256 --tx-buffer 1024 --window-log 3 --rx-fifo 64 --rx-timeout 1 \
  --output results/reliable.json \
  --manifest ~/.cache/roo_transport/esp32-benchmark/firmware/manifest.json

python3 benchmarks/esp32/run.py --port /dev/ttyUSB0 --reset \
  --mode raw-idf --chunk 4096 --samples 3 --output results/raw-idf.json
```

`--reset` pulses RTS/EN with GPIO0 released and keeps the serial connection open
to capture `READY`. A software monitor reboots stalled firmware after twenty
seconds without delivered progress. `--timeout` bounds the host's wait. JSON
results and the console transcript are saved together; `--manifest` attaches
build evidence.

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
- `APP_PRIORITY=0` preserves Arduino main priority and a priority-1 producer;
  other values set both application tasks. `SENDER_PRIORITY=0` uses the library
  default, not priority 0. Nonzero sender priorities require reliable mode.
- RX rings must be at least 129 bytes. `TX_BUFFER=0` selects unbuffered TX;
  positive values size its ring. Buffer setter results are checked.
- `RX_FIFO` selects 1–127 bytes. `RX_TIMEOUT` is 0–101 symbol units; zero disables
  timeout events and can stall control packets below the FIFO threshold.
- `CHUNK` is a positive multiple of 256, up to 4096, for consistent pattern phase.
- `SPLIT_IRQ=0` installs both UARTs on the application core; `--split-irq` installs
  UART1 on core 0 and UART2 on the application core. `CONFIG` reports IRQ cores.
- `--fault-us 50000` forces UART2 RX internally low after 32768 delivered bytes,
  then restores its input route. It does not change either wire's electrical
  drive. Fault injection requires reliable mode and at least 65536 bytes.

## Validation and limits

The paired runner validates integer elapsed times and independently recomputes
payload rates. It reports arithmetic mean and aggregate useful throughput
separately, and preserves latency summaries per batch rather than combining
percentiles. Its offline tests exercise accepted and rejected transcripts.

The single-board runner rejects incomplete data, mismatched settings, wrong
sample numbers, corrupt bytes and observed raw UART errors. Reliable runs require
response EOF and worker shutdown. Fault runs also require
`FAULT fired=1 restored=1`; the tested 50 ms receive break recovers with complete
payload delivery.

Startup events settle before single-board timing; `STARTUP_ERRORS` retains their
classification separately from transfer errors. Raw modes observe break,
buffer-full, FIFO-overflow, framing and parity callbacks. Reliable mode owns
those callbacks, so independent hardware-error counts are unavailable. Payload
and lifecycle checks still apply. Packet counter deltas are not exact wire-loss
estimates.

Primary references: [ESP-IDF UART documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/peripherals/uart.html),
[ESP32 capability limits](https://github.com/espressif/esp-idf/blob/v5.5.2/components/soc/esp32/include/soc/soc_caps.h)
and [Arduino-ESP32 Serial API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html).
