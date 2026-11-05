# ESP32 UART throughput and hardware validation

The current [standalone benchmark example](../../examples/ReliableSerial/Benchmark/README.md)
uses 2 KiB protocol windows, 256-byte RX / 1024-byte TX rings and application/sender
priority 1. With integer retry scheduling, ten consecutive 1 MiB transfers
delivered **3.813356–3.827941 Mbit/s**, mean **3.818552 Mbit/s**. The [November 5–6 follow-up](#november-56-resource-and-scheduling-follow-up)
separates those example measurements from the diagnostic harness and its smaller-buffer
screens. [Follow-up records and transcripts](results/2026-11-06-minimum.json) retain
both source versions and all configurations.

In the original November 5 diagnostic, the connected ESP32 sustained
**3.856 Mbit/s of reliable application payload**
across ten consecutive 1 MiB transfers. Three additional reboots each reproduced
3.8545–3.8570 Mbit/s. This is approximately **99.5% of the 3.875 Mbit/s ideal
full-packet payload ceiling** at the chip's supported maximum of 5 Mbaud.
Both endpoints ran on this one board, at its normal 240 MHz CPU clock.

The main performance gains came from UART configuration: provide more RX FIFO
service margin, use buffered TX, and let the application feed/drain the protocol
windows promptly. A separately tested library fix now drains the buffered UART
backlog on both receive and error notifications. It addresses readiness-event
correctness; it is not a universal speed increase for every configuration.

## Original November 5 diagnostic configuration

- ESP32-D0WD-V3 revision 3.1, 4 MiB flash; Arduino-ESP32 **3.3.7**, ESP-IDF
  **v5.5.2-729-g87912cd291**, pioarduino platform **55.03.37**.
- UART1 TX **GPIO4 → GPIO16** UART2 RX; UART2 TX **GPIO17 → GPIO2** UART1 RX.
- Actual baud **5,000,000** on both UARTs, 8N1, CPU **240 MHz**, APB **80 MHz**.
- Per UART: **8192-byte RX ring**, **1024-byte TX ring**, **64-byte RX FIFO
  threshold**, RX timeout **1** symbol.
- Protocol windows: nominal **16 KiB** each direction; application I/O chunks
  **256 bytes**. Producer and consumer priority **23**, sender priority 23,
  UART event priority 24; 1 kHz RTOS tick.
- Both UART drivers initialized on Arduino main's core **1**. Their interrupts
  therefore share that core. The benchmark also measures separate IRQ cores.
- Local Roo source snapshots; release build, exceptions disabled, UART event
  stack 4096 bytes. Wi-Fi/BLE traffic and unrelated application work were absent.

The benchmark uses the existing native ESP-IDF UART stream specialization of
`ReliableSerial1/2`. ESP32 already has bulk native UART I/O; it does not require
the new Pico DMA backend or a new ESP32 transport implementation.

## Original November 5 results

All rates below count useful application bytes, not UART framing or retransmitted
data. Every delivered byte must match the test pattern. A run also checks EOF
and worker shutdown. [Machine-readable results and transcripts](results/2026-11-05.json)
include actual settings and firmware/source fingerprints.

| Final validation | Samples | Useful Mbit/s |
| --- | ---: | ---: |
| Reliable, sustained | 10 × 1 MiB | **3.854827–3.857503**, mean **3.856220** |
| Reliable, three separate reboots | 3 × 3 × 1 MiB | 3.854524–3.857000 |
| Raw IDF, GPIO4 → GPIO16 | 5 × 1 MiB | 3.992619–3.994954 |
| Raw IDF, GPIO17 → GPIO2 | 5 × 1 MiB | 3.997813–3.999559 |
| Reliable, forced 50 ms receive break | 3 × 1 MiB | Complete correct delivery and recovery |

Normal reliable-run one-byte request/response latency was **0.879 ms median**,
0.639 ms minimum and 1.505 ms p99 in the sustained run's 100 latency samples.
Raw runs reported zero measured FIFO overflows, buffer-full, framing, parity and
break errors in both directions. Reliable-mode UART error counts are explicitly
**unobserved**, since the transport owns those callbacks; its claims are complete
payload validation, retransmission statistics and correct shutdown. Counter
differences around the final FIN/ACK can straddle sample boundaries and are not
an exact wire-loss estimate.

The classic ESP32's 5 Mbaud ceiling yields 4 Mbit/s of raw bytes in 8N1. A full
roo_transport frame carries 248 useful bytes in 256 wire bytes, hence
`5,000,000 × 8/10 × 248/256 = 3,875,000 bit/s` before gaps and control work.
The final result is within about 0.5% of that ceiling.

## Diagnosis

**RX FIFO service margin was the decisive hardware constraint.** The default
threshold of 120 nominally leaves only eight of the FIFO's 128 bytes free:
about **16 µs** at 5 Mbaud. At threshold 64, the nominal margin is **128 µs**.
The interrupt comparator and service latency can consume part of that headroom. Continuous buffered TX runs
its refill ISR while RX also needs service, particularly when both UART ISRs
share a core. The code and experiments support interrupt-service latency as the
mechanism; no instruction trace of the overflow was captured.

Raw transfers at threshold 120 overflowed in both directions. A diagnostic run
reported two FIFO overflows, received only **1,048,539 of 1,048,576 bytes**, and
first diverged at offset 124. Its near-4-Mbit/s printed rate was rejected.
Changing to threshold 64 with the same wires produced complete, clean raw
transfers. More software RX buffering cannot recover bytes lost in the hardware
FIFO. These results establish overflow; they do not establish a new UART HAL
counting defect or a need to change the wiring.

**Buffered TX supports continuous wire traffic.** A 1 KiB TX ring approached
the 4 Mbit/s raw ceiling once RX service was safe. Buffer sizes are not monotonic
performance controls. Ring-buffer fragmentation, task scheduling, IRQ ownership and the
protocol's retransmission timing interact. Large TX queues can also hold more
wire time than the initial retry deadline. The measurements show these
configuration effects; no single mechanism is claimed to explain every slow
case.

**Application scheduling matters.** The original producer/consumer priority 23
let them fill/drain useful batches alongside the priority-23 sender while
priority-24 UART events preempted them. The follow-up below reproduced high rates
at application/sender priority 1. Relative scheduling and batching matter; these
measurements do not require applications to raise their tasks globally to 23.

**The receive callback had a separate functional gap.** It previously consumed
at most 256 raw bytes per readiness/error notification. Buffered bytes could
remain after notifications coalesced or were lost. The new helper snapshots the
readable backlog and drains that byte budget, including partial/corrupt chunks
that produce no valid packets. It stops on no progress and bounds the callback
so continuous fresh data cannot monopolize the high-priority event task. Five
focused fake tests cover those cases and delivery of several valid packets from
one notification. The initial validation passed all **nine** transport host test
targets.

## Controlled code comparison

These pairs use the same final harness, SDK, payloads and explicit settings.
"Before" builds transport source from Git commit `74564ea`; "after" builds the
bounded-backlog fix. Each cell contains two 512 KiB measurements.

| Configuration | Before Mbit/s | After Mbit/s |
| --- | ---: | ---: |
| Original buffer/window/priorities, separate IRQ cores, FIFO120, TX0 | 3.387–3.395 | 2.526–2.534 |
| Shared IRQ core, priority23, FIFO64, TX1024 | 3.024 | 3.851–3.854 |
| Shared IRQ core, priority23, FIFO64, TX2048 | 3.853–3.855 | 2.974–2.982 |
| Shared IRQ core, priority23, FIFO96, TX2048 | 3.849–3.850 | 3.849–3.851 |
| Shared IRQ core, priority23, FIFO64, TX4096 | 3.851 | 3.851–3.852 |

The original code could already approach the wire ceiling with different tuning.
The new callback fixes backlog handling, not every scheduling/performance tradeoff.
Both code versions show strong configuration sensitivity, so the selected final
recipe was validated repeatedly after reboot and over longer transfers rather
than inferred from a short maximum. The original example initializes its UARTs
from separate server/main tasks; an earlier centralized-IRQ baseline reached only
2.68 Mbit/s and should not be conflated with that layout. The historic README
3.4-Mbit/s reference used two separate ESP32s.

## November 5–6 resource and scheduling follow-up

The follow-up first measured the pre-integer transport source represented by
commit `23d2691`, then the integer-backoff source snapshot. Some build manifests
still name that commit while including pending changes; their copied source
hashes identify the tested implementation. A build HEAD alone does not identify
the backoff version. The integer policy is committed in `2bc8cd4`, following
sender-attribute validation in `d2d63ad`; final formatting changes source hashes
without changing that policy. The older results above remain a record of the
initial configuration and source.

Two workloads must remain separate. The diagnostic harness runs its latency
batch once, maintains progress/watchdog counters between application chunks and
accepts runtime commands. The standalone example repeats a latency batch before
each bulk transfer. Its rates must be measured directly rather than inferred
from the diagnostic. All rows below passed payload and finite-lifecycle checks;
`passed` does not mean a configuration met a throughput threshold. Reliable-mode
UART errors remain unobserved because the transport owns the callbacks.

| Workload and source | Windows; RX / TX bytes | App / sender priority | Samples | Useful Mbit/s |
| --- | --- | --- | ---: | ---: |
| Diagnostic, pre-integer | 2 KiB; 256 / 1024 | 1 / 1 | 10 × 1 MiB | **3.851266–3.855381**, mean **3.853364** |
| Diagnostic, current integer source | 2 KiB; 256 / 1024 | 1 / 1 | 3 × 1 MiB | 3.687678–3.694374, mean 3.690453 |
| Diagnostic, integer source, forced 50 ms break | 2 KiB; 256 / 1024 | 1 / 1 | 2 × 1 MiB | 3.578510–3.683906; recovery passed |
| Standalone example, pre-integer | 2 KiB; 256 / 1024 | 1 / 1 | 3 × 1 MiB | 3.537897–3.561922 |
| Same example, integer backoff | 2 KiB; 256 / 1024 | 1 / 1 | 3 × 1 MiB | **3.802957–3.827351** |
| Canonical example default, integer backoff | 2 KiB; 256 / 1024 | 1 / 1 | 10 × 1 MiB | **3.813356–3.827941**, mean **3.818552** |
| Example, integer backoff | 2 KiB; 512 / 1024 | 1 / 1 | 3 × 1 MiB | 3.822422–3.832103 |
| Example, integer backoff | 2 KiB; 256 / 576 | 1 / 1 | 3 × 1 MiB | 3.802650–3.807706 |
| Example, integer backoff, FIFO96 | 2 KiB; 256 / 1024 | 1 / 1 | 3 × 1 MiB | 3.777939–3.785972 |
| Example, integer backoff, smaller window | 1 KiB; 256 / 1024 | 1 / 1 | 3 × 1 MiB | 3.648584–3.662914 |

The three-cycle pre-/post-backoff example rows use the same sketch source hash
and UART settings. Changing retry scheduling improved that standalone workload.
It did not improve every workload: the current diagnostic averaged 3.690453 Mbit/s
at the same nominal low-resource settings, below its pre-integer 3.853364 Mbit/s.
The diagnostic's pacing and scheduling differ from the example; the older
harness rate must not be assigned to the current source or standalone recipe.
The separate RX512, TX576 and FIFO96 rows are probes, not changes to the example defaults. The default
remains RX256, TX1024, FIFO64, 2 KiB windows and priority 1 for both application
and sender. The current integer 1 KiB-window screen also passed correctness,
but at 3.648584–3.662914 Mbit/s, so 2 KiB remains the practical default. UART event
tasks retain the SDK priority 24. The final canonical default completed all ten
1 MiB cycles with exact payloads, response EOF, worker shutdown and `DONE`.

Current host validation passes all 12 transport test targets and 38 offline
runner checks. Native ESP32 builds also compile both `MODE_SERVER` and
`MODE_CLIENT`; a guarded public-header smoke check passes with the standard C++
thread backend. The updated diagnostic harness also compiled against legacy
transport source `74564ea`. These compile checks cover those entry points, while
the throughput numbers come from the real loopback.

The final current-source diagnostic also recovered from a forced 50 ms receive
break over two 1 MiB transfers. It reported `FAULT fired=1 restored=1`, complete
correct payloads, response EOF and normal worker shutdown. Its rates include the
fault workload; this checks recovery, not a higher-throughput configuration.

Earlier pre-integer screens reached about 3.84 Mbit/s with RX129/TX1024 and
3.83 Mbit/s with RX256/TX576 over two 256 KiB samples. RX129/TX768 also completed
three 1 MiB samples at 3.822179 Mbit/s mean. TX528 and TX544 each had samples below
the roughly 3.779 Mbit/s floor used for the screen, despite passing correctness;
TX512 with RX1024 averaged 3.764163 Mbit/s. Default 4 KiB windows also reached
3.850964 Mbit/s in a short diagnostic check, although the standalone example's
4 KiB probe was slower. These observations establish tested combinations rather
than interchangeable minima. Short screens, different source versions and
individually reduced buffers cannot be combined into an untested recipe.

Ring sizes and nominal protocol windows are per endpoint. Buffer metadata,
thread stacks, SDK event queues and other driver allocations are additional.
`begin(const roo::thread::attributes&)` configures only the sender, copies the
complete attributes and does not merge them with defaults. Set a 4096-byte stack
explicitly when changing priority or name while retaining the existing sender
stack budget. The no-argument overload preserves its historical defaults;
receiver/event-task scheduling is unaffected.

The same example automatically selects native DMA for supported RP2040 FreeRTOS
builds. Its practical Pico default is 8 KiB windows with application/sender
priorities 1/1, confirmed over ten 1 MiB cycles at 4.333691–4.468476 Mbit/s
(mean 4.396369). The peak-tested 16 KiB, 6/6 profile reached 4.65–4.74 Mbit/s
in three cycles. A 4 KiB, 1/1 profile
traded more rate for memory at 3.81–3.87 Mbit/s. See the [Pico resource table](../pico/README.md#november-56-follow-up-backoff-cost-and-resource-probes)
for those separate board measurements and their confirmation limits.

### Retry computation and receive work

Every data packet computes its next retry deadline, including first sends.
`ThreadSafeTransmitter::send()` holds the transmit mutex while
`Transmitter::getBufferToSend()` calls `OutBuffer::markSent()`. The previous
`pow(1.33, retry_count)` and floating-point jitter therefore also extended time
holding the mutex used by producers and ACK processing. The shared integer helper
removes those operations from both data and handshake retry scheduling.

The base delay grows by 1.33 with nearest-microsecond rounding at each step and
saturates before jitter. Data uses a 5000 µs initial base and 200000 µs cap; the
first send uses retry count 1, giving a 6650 µs base. Handshakes start at count 0
with 1000 µs and cap at 1000000 µs. RNG draws are mapped from their actual maximum
to 8192 bins within ±20% of the base. The radius rounds down to whole microseconds,
the upper endpoint is excluded, and the maximum bin spacing is 49 µs. Previously,
a full 32-bit hardware draw divided by the smaller `RAND_MAX` could produce up
to +60% jitter, violating the stated ±20% policy. The range normalization fixes
that bug as well as removing floating-point work.

[ESP-IDF documents automatic task pinning after `float` use](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/system/freertos_idf.html#floating-point-usage).
Removing per-packet floating-point operations also removes that source of implicit
pinning. This does not establish affinity as the cause of the earlier
example/diagnostic gap: instrumented versions had their two senders pinned to
cores 0 and 1 in both programs. The CPU and scheduling contributions were not
isolated. Runtime-profile variants add instrumentation and are diagnostic data,
not minimum-resource recipes.

Small partial UART reads still pay driver locking and ring-buffer costs. The
packet receiver also copies incomplete frames into its assembly buffer, while
complete frames can be decoded directly from the read buffer. Amortizing those
costs is useful, but increasing the FIFO threshold reduces overflow-service margin.
FIFO96 was slightly slower in the example probe above, so the current default
retains FIFO64. Larger application chunks also failed to improve the pre-integer
example consistently; UART rings, application chunks and protocol windows are
separate tuning choices.

## Build

Keep the Roo repositories as sibling source checkouts. The builder snapshots
those sources rather than fetching published Roo dependencies, records repository
commits, source SHA-256 hashes, build flags and binary hashes, and leaves upload
to the caller:

```sh
python3 benchmarks/esp32/build.py \
  --platform /path/to/installed/espressif32 \
  --board esp32dev
```

The default output is
`~/.cache/roo_transport/esp32-benchmark/firmware`. Builds use stock 240 MHz CPU,
exceptions disabled and a 4096-byte Arduino UART event stack. Runtime library
dependency discovery uses `deep` because ESP-IDF capability macros can arrive
through compiler response files that the conditional `deep+` scanner does not see.
UART event task core/priority can be varied independently with
`--uart-event-core -1/0/1` and `--uart-event-priority N` when supported by the core.

The build does not upload. Preserve the complete original flash before installing
the benchmark. Classic ESP32 images also require matching bootloader and partition
images; do not assume its original partition map matches this build. The generated
PlatformIO configuration uses 115200 upload speed for conservative USB-UART bridge
compatibility.

## Run

Install pyserial and invoke the runner with the detected console port:

```sh
python3 benchmarks/esp32/run.py --port /dev/ttyUSB0 --reset \
  --baud 5000000 --samples 10 --application-priority 1 --sender-priority 1 \
  --rx-buffer 256 --tx-buffer 1024 --window-log 3 --rx-fifo 64 --rx-timeout 1 \
  --output results/reliable.json \
  --manifest ~/.cache/roo_transport/esp32-benchmark/firmware/manifest.json

python3 benchmarks/esp32/run.py --port /dev/ttyUSB0 --reset \
  --mode raw-idf --chunk 4096 --samples 3 \
  --output results/raw-idf.json
```

Use the corresponding COM port on Windows. `--reset` pulses the USB-UART
RTS/EN reset line with GPIO0 released, booting the application rather than the ROM
loader. It keeps the same serial connection open to retain the early `READY`
line. A software monitor also reboots firmware after twenty seconds without
delivered progress. Task watchdog settings are not changed.

This runner controls the diagnostic harness. To build the standalone example,
pass `--sketch examples/ReliableSerial/Benchmark/Benchmark.ino` to the builder;
that sketch starts after reset and uses compile-time settings instead of `run`.

The host rejects incomplete runs, wrong command/configuration echoes, payload
mismatches, truncated transfers, unexpected sample numbers and observed raw UART
errors. Reliable runs also require successful stream EOF and worker shutdown.
JSON records and the original console transcript are saved together.

## Parameters

The firmware accepts:

```text
run BAUD RX_BUFFER TX_BUFFER WINDOW_LOG MODE BYTES SAMPLES APP_PRIORITY RX_FIFO RX_TIMEOUT CHUNK [SPLIT_IRQ [FAULT_US [SENDER_PRIORITY]]]
```

- `MODE=0`: `ReliableSerial1/2`, including their native IDF stream specialization.
- `MODE=1`: raw `HardwareSerial.write/readBytes`.
- `MODE=2`: raw `uart_write_bytes/uart_read_bytes`, with UART driver installation
  and pin routing still handled by `HardwareSerial.begin`.
- `MODE=3`: reverse raw IDF transfer, UART2 TX GPIO17 → UART1 RX GPIO2.
  This checks the wire carrying acknowledgments during the forward reliable test.
- `WINDOW_LOG=3/4/5/6`: nominal 2/4/8/16 KiB protocol windows.
- `APP_PRIORITY=0`: preserve Arduino's main priority and the example's priority-1
  producer. Other values set both application tasks to the supplied priority.
- `SENDER_PRIORITY` is optional parameter 14 (`--sender-priority` in the runner).
  Zero retains the library's historical sender default, 23 on this SDK; it does
  not request priority 0. A nonzero value configures both reliable sender workers
  through `begin(attributes)` and is accepted only in reliable mode. It does not
  change the UART event task's priority.
- `RX_FIFO` and `RX_TIMEOUT` select Arduino's receive FIFO full threshold and
  timeout in UART symbol units (0–101 for 8N1). These affect UART interrupt/event
  delivery. Zero disables timeout events and can stall short control packets if
  they do not reach the FIFO threshold. All threshold/timeout setter results are
  checked rather than silently recording an unsupported setting.
- `TX_BUFFER=0` uses unbuffered driver TX; 256, 1024 and 8192 permit controlled TX
  buffering comparisons. RX/TX buffer setter return values are checked.
- `CHUNK` is a positive multiple of 256, at most 4096. This keeps the repeating
  pattern phase consistent while exposing application batching cost.
- `SPLIT_IRQ=0` (default) installs both UART drivers on the application core.
  `SPLIT_IRQ=1` initializes UART1 in a temporary task pinned to core0 before
  configuring UART2 on the application core. This isolates UART interrupt core
  ownership from UART event callback task affinity. The runner flag is
  `--split-irq`; `CONFIG` reports `split_irq`, `irq1_core` and `irq2_core`.
- `FAULT_US=0` (default) leaves input routing alone. `--fault-us 50000` forces
  UART2 RX internally low for 50 ms after at least 32768 payload bytes have been
  delivered. It changes only the peripheral input matrix; neither crossed wire
  is driven differently. The exact previous input-route register is restored
  before teardown. This requires reliable mode and at least 65536 bytes.
  Acceptance requires `FAULT fired=1 restored=1` as well as complete byte-checked
  payload delivery, EOF and normal worker shutdown.

The initial raw command for this wiring is:

```text
run 5000000 8192 1024 6 2 1048576 3 23 64 1 4096 0 0
```

To check the reverse wire independently, use the same raw command with `MODE=3`
(or `--mode raw-idf-reverse` in the public runner).

The corresponding reliable command is:

```text
run 5000000 256 1024 3 0 1048576 10 1 64 1 256 0 0 1
```

Reliable mode performs 100 one-byte request/response latency samples, then the
configured payload transfers, validates EOF, closes application streams, joins
the server and stops both reliable transports. Link objects outlive their reader
and writer adapters throughout cleanup.

Before starting any timed work, the firmware lets UART startup events settle,
flushes both receive paths, drains pending event callbacks, records
`STARTUP_ERRORS`, and resets the measured error counters. A crossed RX pin can
otherwise report a framing error while the peer UART's TX is still initializing.
Raw error records classify break, software-buffer-full, FIFO-overflow, framing
and parity errors separately; startup errors remain in the saved transcript.

Raw modes report UART error callbacks. Reliable mode marks those counters
`observed=0` because the production transport owns and replaces the callbacks;
it still validates complete delivered payloads and reports retransmission
counters. Packet counters can straddle sample boundaries, especially during the
last stream-close exchange, so their ratio is not an exact wire loss estimate.

To reproduce the original-code comparison with this same harness, pass
`--transport-ref 74564ea` to `build.py`. The default builds the current worktree.
This option affects the local transport snapshot, without checking out or changing
the repository. The manifest records the selected reference and actual sources.
The compatibility dispatch also compiles with that older API. Keep
`--sender-priority 0` for revisions without `begin(attributes)`; an explicit
sender override requires the newer API and is rejected by older revisions.

## Restoration and provenance

The original complete 4 MiB flash was backed up before installing any benchmark
image. USB/CH340 flash-read transfers lost bytes with default 4096-byte read
blocks, even at low programming baud. Reading 1024-byte blocks in digest-checked
64 KiB chunks succeeded; a final on-device checksum matched the assembled full
image. After the November 5–6 follow-up, the complete original 4 MiB image was
restored and its full on-device digest verified. Flash backups and benchmark binaries stay in the local cache,
not in this repository. The programming USB link is separate from the two UART
wires used for throughput measurements.

These results cover one classic ESP32 running both endpoints, the named SDK and
the stated workload. They do not establish two-board, long-cable or loaded
Wi-Fi/BLE performance. The 256-byte repeating pattern plus exact byte counts
checks the benchmark data; separate protocol tests exercise ordering and retries.

Primary references: [ESP-IDF 5.5.2 UART documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/peripherals/uart.html),
[ESP32 capability limits](https://github.com/espressif/esp-idf/blob/v5.5.2/components/soc/esp32/include/soc/soc_caps.h),
and [Arduino-ESP32 Serial API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html).
