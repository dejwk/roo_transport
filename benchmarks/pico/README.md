# RP2040 UART throughput and hardware validation

[`roo_transport::pico::ReliableUartDma`](../../src/roo_transport/link/pico/reliable_uart_dma.h)
provides reliable byte streams over native RP2040 UART RX/TX DMA. An RP2040-Zero
loopback sustains about **4.40 Mbit/s of useful payload** with 8 KiB protocol
windows and priority-1 application/sender tasks. A 16 KiB-window, priority-6
configuration reaches **4.65–4.74 Mbit/s**.

The [benchmark example](../../examples/ReliableSerial/Benchmark/README.md) selects
DMA automatically on native RP2040 Arduino-Pico FreeRTOS builds. General
`ReliableSerial1` / `ReliableSerial2` instances use the Arduino UART adapter.
The DMA backend requires native RP2040 hardware and the roo_threads FreeRTOS
backend; host emulation does not model DMA.

## Hardware and configuration

The measured setup uses an RP2040-Zero with RP2040 B2, 2 MiB flash, a 133 MHz CPU,
Arduino-Pico 5.4.3 and FreeRTOS SMP: two cores, eight priorities and a 1 kHz tick.
Both endpoints run on the same board with short crossed UART wires:

- UART0 TX **GP12 → GP5** UART1 RX.
- UART1 TX **GP4 → GP13** UART0 RX.
- Actual UART rate **8,312,500 baud**, 8N1 framing.
- `clk_peri` derived from `clk_sys` at 133 MHz; the CPU clock stays at 133 MHz.
- Aligned **8 KiB RX** and **8 KiB TX** DMA rings per endpoint.
- Practical profile: **8 KiB send/receive windows**, application/sender priorities
  **1/1**, receive-worker priority **7**, and 256-byte application chunks.

Clock and baud policy belong to the application. A 48 MHz peripheral clock caps
these UARTs at 3 Mbaud, or 2.4 Mbit/s of raw byte payload with 8N1 framing.
Selecting `clk_sys` for `clk_peri` permits the measured rate without overclocking
the CPU. Configure other peripherals for that clock choice.

Keep application producers and transport senders at matching priorities so the
producer can fill protocol windows while receive workers service incoming data.
Windows must cover the data in flight until acknowledgments arrive. Larger
windows cost RAM; larger DMA rings alone do not remove protocol or CPU costs.
Receive workers poll once per RTOS tick, which also affects round-trip latency.

## Measured throughput

The table reports delivered, pattern-validated payload, in decimal Mbit/s. UART
start/stop bits, protocol framing, checksums and retransmissions consume wire
bandwidth but do not count as useful payload. These are one-board loopback
measurements, not two-board or long-cable guarantees.

| Profile | Window per direction | Application/sender priority | Samples × payload | Payload Mbit/s range | Mean Mbit/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Practical example profile | 8 KiB | 1/1 | 10 × 1 MiB | **4.333691–4.468476** | **4.396369** |
| Larger window | 16 KiB | 1/1 | 3 × 1 MiB | 4.552240–4.618967 | 4.582844 |
| Highest tested throughput | 16 KiB | 6/6 | 3 × 1 MiB | **4.654853–4.743518** | **4.694295** |
| Lower memory | 4 KiB | 1/1 | 3 × 1 MiB | 3.814649–3.871041 | 3.843671 |
| Smaller window | 2 KiB | 1/1 | 3 × 1 MiB | 3.022296–3.039832 | 3.030406 |

With the practical profile, ten batches of 100 one-byte round trips report
median latencies of 2.576–2.655 ms and p99 latencies of 2.968–3.746 ms per batch.
The receive worker's tick-based polling contributes to those round-trip times.

Every listed run validates all payload bytes and completes response EOF and
worker shutdown. Protocol windows are nominal payload capacities per direction;
metadata, DMA rings and thread stacks require additional RAM. These tested
combinations do not establish a global minimum buffer size or task priority.

The diagnostic harness includes progress/watchdog work and a latency sequence
that differ from the illustrative example. Its 8 KiB, priority-1 profile measures
3.755584–3.795070 Mbit/s over three 1 MiB samples. Use rates from the relevant
workload when selecting a configuration.

[Measurement records and transcripts](results/2026-11-06-minimum.json) contain
per-run parameters, firmware/source fingerprints and validation outcomes. Those
fingerprints identify the exact measured implementation; the setup above
describes the configuration to reproduce. Raw packet counter differences are
diagnostics and are not an exact estimate of wire loss.

## Driver lifetime and resources

Configure each SDK UART's pins, framing and baud before constructing
`ReliableUartDma`. Configure all active UARTs before either DMA endpoint is
constructed, because construction immediately arms reception. Do not also use
Arduino `Serial1/2` or another driver on a UART owned by DMA.

Supply a caller-owned 8 KiB RX buffer aligned to 8 KiB, and keep it alive for the
transport's entire lifetime. Each endpoint claims two DMA channels, a hardware
spinlock and its own 8 KiB TX ring, in addition to protocol windows and stacks.
Resource exhaustion is fatal.

Construct and destroy all DMA instances serially on one core. Their completion
handler shares `DMA_IRQ_0` with other DMA users on that core; keep the IRQ enabled
only on that core. Arduino-Pico's setup task is suitable for this ownership.
Stop application I/O and destroy `Link` objects and their readers/writers before
calling `end()`. An ended DMA instance cannot restart; create another instance
when reusing the hardware.

`begin()` uses sender/receiver priorities `configMAX_PRIORITIES - 2` and
`configMAX_PRIORITIES - 1` (6 and 7 in the measured configuration). Constructor
window defaults are 16 KiB per direction; the practical example explicitly
selects 8 KiB. To select priority-1 senders, pass complete attributes:

```cpp
roo::thread::attributes sender;
sender.set_name("uart-send");
sender.set_stack_size(4096);
sender.set_priority(1);
uart.begin(sender);
```

The overload copies all attributes without merging defaults. It changes only
the sender; the receive worker keeps its higher priority. Connect through
`transport()` and use the resulting `Link`'s roo_io streams. The Arduino UART
adapter is also available, but its idle receive worker stays alive until reset.
Shared transport tests run in ESP32 host emulation; DMA ownership and hardware
error handling are validated on RP2040 hardware.

## Reproduce

Keep the Roo repositories as sibling checkouts. Install Python, pyserial and an
Arduino-Pico-capable PlatformIO platform, then build the diagnostic firmware:

```sh
python3 benchmarks/pico/build.py \
  --platform /path/to/installed/platform-raspberrypi \
  --board waveshare_rp2040_zero
```

Output defaults to `~/.cache/roo_transport/pico-benchmark/firmware`. The builder
copies the local Roo sources and writes `firmware.uf2` plus a manifest identifying
the sources, build settings and binary. Building does not upload firmware.

Load the UF2 onto the board, then run the practical diagnostic profile:

```sh
python3 benchmarks/pico/run.py --port /dev/ttyACM0 \
  --baud 8312500 --window-log 5 \
  --application-priority 1 --sender-priority 1 \
  --samples 3 --output results/profile.json \
  --manifest ~/.cache/roo_transport/pico-benchmark/firmware/manifest.json
```

Use the board's COM port on Windows. `--reset` reboots firmware that is waiting
for a command or has completed a run. The runner saves console output and rejects
incomplete transfers, payload mismatches, DMA overruns and unexpected UART errors.
DMA runs must also pass the idle stop/reuse lifecycle check.

## Diagnostic modes and command

| Runner mode | Command value | Purpose |
| --- | ---: | --- |
| `dma` | 10 | Reliable native DMA transport |
| `arduino` | 0 | Reliable Arduino UART adapter |
| `original-layout` | 5 | Arduino UARTs initialized from application workers |
| `raw-dma` | 7 | Raw DMA byte transfer without the reliable protocol |
| `raw-arduino` | 1 | Raw Arduino UART transfer |
| `read-cost` | 2 | CPU cost of reading buffered Arduino UART bytes |
| `cpu-cost` | 9 | Protocol/processing CPU measurements |

The firmware accepts this USB command:

```text
run BAUD FIFO WINDOW_LOG MODE BYTES SAMPLES SYS_PERI APP_PRIORITY FAULT_US SENDER_PRIORITY
```

`WINDOW_LOG=3/4/5/6` means 2/4/8/16 KiB. `SYS_PERI=1` selects `clk_sys` for
`clk_peri`; `--peri-clock usb` retains the 48 MHz peripheral clock.
`SENDER_PRIORITY=1..6` applies to DMA mode, while zero or omission selects the
default sender priority 6. `APP_PRIORITY=0` leaves the main task's priority
unchanged and runs the producer at priority 1. Neither setting changes the
receive worker's priority 7. A watchdog resets stalled firmware; a reset does
not turn a partial transfer into an accepted measurement.

## Recovery and counter renewal

A receive-break check forces the GP5 input low internally for 50 ms without
driving either physical wire:

```sh
python3 benchmarks/pico/run.py --port /dev/ttyACM0 --reset \
  --baud 8312500 --window-log 5 \
  --application-priority 1 --sender-priority 1 \
  --samples 2 --fault-us 50000 --output results/recovery.json
```

The measured check delivers two complete 1 MiB payloads at
3.768885–3.871779 Mbit/s, records the injected receive error, reports zero DMA
overruns, and completes EOF, worker stop and five idle stop/reuse cycles. These
rates describe recovery work, rather than a throughput tuning target.

For DMA counter-renewal checks, build with `--test-rx-count 65536` and run multi-MiB
transfers. This forces frequent renewals rather than waiting roughly 90 minutes
for the normal 32-bit count to expire at 8 Mbaud. Renewal preserves RX progress
across DMA abort and ring wrap; five 1 MiB transfers with the forced count pass
payload, EOF, stop/reuse, overrun and UART-error checks. The [counter-renewal
records](results/2026-11-05.json) identify that test firmware separately.

Implementation references: [Arduino-Pico UART source](https://github.com/earlephilhower/arduino-pico/blob/5.4.3/cores/rp2040/SerialUART.cpp),
[Arduino-Pico FreeRTOS documentation](https://arduino-pico.readthedocs.io/en/stable/freertos.html),
and [Pico SDK hardware documentation](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html).
