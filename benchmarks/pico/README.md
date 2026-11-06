# RP2040 UART throughput and hardware validation

The November 5 published diagnostic run on an RP2040-Zero with crossed UARTs
reached **3.92 Mbit/s of reliable application payload** (3.87 Mbit/s mean over ten
consecutive 1 MiB transfers), with the CPU at its normal 133 MHz. The November 5–6
follow-up below measures the reusable example with integer backoff. Its practical
8 KiB-window, priority-1 profile completed **ten 1 MiB cycles at 4.333691–4.468476
Mbit/s**, with a mean of **4.396369 Mbit/s**. The peak tested 16 KiB-window,
priority-6 profile reached **4.65–4.74 Mbit/s** over three 1 MiB cycles.
Both protocol endpoints ran on this one board. Every delivered byte was checked
against a known pattern; throughput excludes UART framing, packet headers,
checksums and retransmissions.

The fast path is [`roo_transport::pico::ReliableUartDma`](../../src/roo_transport/link/pico/reliable_uart_dma.h).
It uses the existing framing and reliable protocol, with native UART RX/TX DMA.
Existing `ReliableSerial1` / `ReliableSerial2` users retain Arduino's UART backend;
they do not automatically get DMA or the rates above.

## Hardware and toolchain

- RP2040-Zero, RP2040 B2, 2 MiB flash, stock 133 MHz CPU.
- UART0 TX **GP12 → GP5** UART1 RX; UART1 TX **GP4 → GP13** UART0 RX.
- Arduino-Pico **5.4.3**, PlatformIO framework package **1.50403.0**;
  installed maxgerhardt Raspberry Pi platform snapshot `ff76a3915224135aafad379817f41edd`.
- FreeRTOS SMP: two cores, eight priorities, 1 kHz tick. Release build with `-Os`,
  exceptions and RTTI disabled. USB CDC provides only commands/results.
- Current practical example configuration: actual **8,312,500 baud**, 8N1;
  peripheral clock explicitly derived from `clk_sys` at 133 MHz; 8 KiB
  send/receive protocol windows; producer, consumer and sender priorities 1.
  The RX and TX DMA rings remain 8 KiB each per endpoint, and the receive
  worker remains at priority 7. This practical profile passed the final
  canonical ten-cycle example run. The peak tested profile uses 16 KiB windows
  and application/sender priorities 6/6 and was screened over three cycles.

The benchmark changes `clk_peri` before initializing UARTs. The library itself
leaves clocks and baud under application control. Other peripherals using that
clock must be configured accordingly. The CPU is not overclocked.

## Historical measurements: November 5

The ablation rows are investigation snapshots; the published harness reproduces
the final DMA, Arduino and raw-UART configurations. Earlier prototype backends
are not part of the library.

All rates are decimal Mbit/s. Each listed range contains separate measured runs,
not a rate inferred from baud. Logs and firmware/source fingerprints accompany
the [result records](results/2026-11-05.json).

| Configuration | Samples × payload | Useful payload Mbit/s |
| --- | ---: | ---: |
| Original Arduino UART transport, 115,200 baud | 2 × 16 KiB | 0.0876–0.0877 |
| Arduino UART transport with receiver pinned, 3 Mbaud | 3 × 64 KiB | 0.0866–0.0868 |
| Raw DMA, default 48 MHz peripheral clock, requested 5 Mbaud / actual 3 Mbaud | 3 × 1 MiB | 2.3999 |
| Raw DMA, actual 7.940298 Mbaud | 3 × 1 MiB | 6.3491–6.3502 |
| Reliable DMA, actual 5.018867 Mbaud | 3 × 1 MiB | 3.2013–3.2335 |
| Reliable DMA, actual 7.940298 Mbaud | 10 × 1 MiB | 3.5725–3.6482 |
| Reliable DMA, actual 8.3125 Mbaud, 16 KiB windows, final release | 10 × 1 MiB | **3.8404–3.9203** |
| Same driver, forced RX counter renewal every 64 KiB | 5 × 1 MiB | 3.7835–3.8512 |
| Raw DMA, actual 8.3125 Mbaud | 3 × 1 MiB | 6.6495 |
| Same baud, 8 KiB windows | 5 × 1 MiB | 3.5614–3.6812 |

Normal DMA runs reported zero payload mismatches, RX ring overruns and UART receive
errors. Packet counters can straddle sample boundaries by one packet; use the raw
counts as diagnostics rather than interpreting their ratio as exact wire loss.
The final 8.3125 Mbaud run's median one-byte request/response latency was 2.786 ms.
Receive workers currently poll once per RTOS tick; this is not a low-latency driver.

## What limited the old path

1. **The requested baud was not necessarily the actual baud.** With this installed
   core's clock configuration, `clk_peri` is 48 MHz despite the CPU running at
   133 MHz. The UART's minimum divider makes 3 Mbaud the ceiling. 8N1 then limits
   raw byte payload to 2.4 Mbit/s before protocol overhead. Requesting 5 Mbaud
   silently produced 3 Mbaud. Selecting the system-derived peripheral clock
   enables the higher actual rates measured above.
2. **Arduino UART operations cost too much per byte.** Consuming 25,600 bytes
   already buffered by the IRQ took 606,681 µs: only 0.338 Mbit/s. This isolates
   CPU/API cost from wire arrival time. `Stream::readBytes()` loops over byte
   reads, while `SerialUART::read()` takes `CoreMutex` and pumps its FIFO per byte.
   Its `availableForWrite()` reports one byte, so the generic Arduino output
   adapter also subdivides packet writes. Enlarging the software FIFO did not
   address that work or the observed stalls. Native bulk RX and asynchronous TX
   DMA remove these costs and let transmission overlap protocol processing.
3. **The SMP receive worker could migrate through core-local IRQ handling.**
   The original example layout stalled at 1 Mbaud. Pinning the receive worker to
   the core that calls `begin()` made repeated 3 Mbaud transfers complete. The
   source-level explanation is SerialUART's core-local UART IRQ masking around
   FIFO pumping; migration can restore that state on a different core. This is
   the supported diagnosis from the code and affinity experiment; an instruction
   trace of the original stall was not captured. Initialize the Arduino UART on
   that same core. The DMA path avoids this SerialUART code entirely.
4. **Control checks and scheduling were repeated for every data packet.** With
   bulk native RX but blocking TX, useful throughput plateaued near 1.5 Mbit/s.
   Adding TX DMA reached about 1.8 Mbit/s. Bounded bursts of eight data packets
   raised it to about 3.4 Mbit/s, keeping control checks between bursts and the
   existing wire protocol. Coalescing already-pending sender notifications avoided
   more mutex work and raised it further. Focused tests cover burst bounds,
   bidirectional delivery and notification/wait races.
5. **Application priorities determine whether bursts can fill.** A producer below
   the sender's priority is preempted as soon as it makes one packet available.
   In the same DMA experiment, the old low producer priority delivered about
   1.4 Mbit/s; priority 6 let it fill useful bursts. Priorities are an explicit
   application configuration, not changed globally by the library.

At 5 Mbaud the final release shows more retransmissions than at 8.3125 Mbaud.
The asynchronous TX queue can hold more wire time than the protocol's initial
retransmission deadline, so queueing and late ACKs can cause premature retries.
A smaller TX queue or queue-aware retransmission timing is a remaining tuning
opportunity; payload integrity and recovery still passed.

Eight-packet bursts were the best tested balance. Sixteen gave little gain and
more retries. Pinning each complete endpoint to a separate core reduced
throughput. A reader-wakeup optimization also regressed throughput and was not
retained. Whole-program `-O3` regressed to about 1.9 Mbit/s, so the benchmark keeps
`-Os`; the cause of that regression was not isolated. A CPU microbenchmark found
an uncontended mutex pair cost about 19 µs and a 248-byte Murmur hash about
19 µs. Raising baud without reducing software work therefore could not solve the
original problem.

These historical measurements indicated a software/CPU scheduling ceiling on a
two-endpoint loopback, rather than a demonstrated wire limit. The existing ESP32
3.39 Mbit/s reference used **two separate ESP32s**. A two-Pico measurement is still
needed to establish its maximum.

## November 5–6 follow-up: backoff cost and resource probes

The actual [Benchmark example](../../examples/ReliableSerial/Benchmark/Benchmark.ino)
with integer backoff reached a peak tested **4.654853–4.743518 Mbit/s** over three
1 MiB transfers (mean **4.694295 Mbit/s**), with zero bad payload bytes. This used
8.3125 Mbaud, 133 MHz CPU/peripheral clocks, 16 KiB send/receive windows,
8 KiB RX/TX DMA rings,
256-byte application chunks, and producer/sender priorities 6/6. The preceding
example comparison with the same sketch source hash reached 3.185256–3.239179
Mbit/s. These are actual-example measurements, separate from the earlier 3.87
Mbit/s diagnostic mean. Source fingerprints and the individual records are in the
[November 6 follow-up results](results/2026-11-06-minimum.json).

The measured pre-integer snapshot has `OutBuffer` and `Channel` source hashes
`a35677084ce3…` and `f230ed5596c8…`. The measured integer peak snapshot has hashes
`fd3f7a265d7c…` and `e9b013f10040…`, plus `retry_backoff.h` hash
`fc6e64d3f33e…`. Both paired example comparisons used sketch hash
`eec3513fbed5…`. These are hashes of the measured snapshots, not promises that
later formatting or API guard changes leave every file hash identical.

Older manifests record repository marker `23d2691`; the sender-attributes API
history was subsequently rewritten as `d2d63ad`, including guard refinements.
The integer-backoff policy is now published in `2bc8cd4`. The later resource
screens record `Channel` hash `c6f2fd04c151…` with the same measured integer
helper and `OutBuffer` hashes. Formatting in subsequent canonical builds can
change source hashes without changing that policy. The older repository markers
do not prove which working-tree changes were built; use the per-run source
hashes for reproduction.

Backoff grows by 1.33 per retry, rounding to the nearest microsecond at each
step. The nominal delay saturates before applying up to ±20% jitter: data uses
a 5 ms initial delay and 200 ms cap, while handshake uses 1 ms and 1 s. The
integer helper normalizes the actual RNG range, including a full 32-bit
hardware range, into 8192 bins (13 bits). The upper jitter endpoint is excluded;
adjacent bins are at most 49 microseconds apart at the largest cap. This replaces
floating-point `pow()` on the send path. The old division of a full-width
hardware draw by `RAND_MAX` could yield up to +60% jitter, violating the stated
±20% policy; normalization by the actual RNG range fixes that bug. In particular,
`ThreadSafeTransmitter::send()` holds its mutex while
`Transmitter::getBufferToSend()` calls `OutBuffer::markSent()` to schedule the
next expiration. That happens on the first transmission as well as retries, so
the arithmetic cost applies to every newly sent packet.

The public `begin(const roo::thread::attributes&)` overload copies the complete
sender configuration. It does not merge defaults: set the name, priority and
4096-byte stack explicitly when retaining that stack budget. The receive worker
keeps priority 7. The example selects native DMA only for RP2040 + Arduino-Pico
FreeRTOS with `ROO_TESTING` absent; general `ReliableSerial1/2` users keep the
Arduino UART backend.

The final canonical practical-profile example completed ten 1 MiB cycles at
4.333691–4.468476 Mbit/s (exact arithmetic mean 4.3963687 Mbit/s). Every payload
byte, EOF, worker stop and `DONE` check passed. The earlier resource screens each
ran three complete 1 MiB cycles with those checks; their coverage is distinct
from the final ten-cycle run. Windows are nominal sizes for each send/receive
direction; RX/TX DMA rings remain fixed at 8 KiB each per endpoint in every row.

| Profile | Window per direction (`WINDOW_LOG`) | Application/sender priority | Cycles × payload | Payload Mbit/s range | Mean Mbit/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Peak tested | 16 KiB (`6`) | 6/6 | 3 × 1 MiB | 4.654853–4.743518 | 4.694295 |
| Lower priorities | 16 KiB (`6`) | 1/1 | 3 × 1 MiB | 4.552240–4.618967 | 4.582844 |
| Practical default, canonical confirmation | 8 KiB (`5`) | 1/1 | 10 × 1 MiB | 4.333691–4.468476 | 4.396369 |
| Earlier practical-profile screen | 8 KiB (`5`) | 1/1 | 3 × 1 MiB | 4.379166–4.390346 | 4.383114 |
| Lower memory | 4 KiB (`4`) | 1/1 | 3 × 1 MiB | 3.814649–3.871041 | 3.843671 |
| Smallest window screened here | 2 KiB (`3`) | 1/1 | 3 × 1 MiB | 3.022296–3.039832 | 3.030406 |

The practical example uses 8 KiB windows and priorities 1/1, validated by the
canonical ten-cycle run, trading some peak throughput for smaller protocol
windows and lower application/sender priorities. The 16 KiB, 6/6 profile remains
the peak tested configuration with three-cycle coverage. Earlier probes using
floating-point backoff are historical controls for that older source. Smaller
independent passing settings do not qualify an untested combined profile, and
these one-board measurements do not establish a global hardware minimum.

## Reproduce

Keep the Roo repositories as sibling checkouts. Install Python, pyserial and an
Arduino-Pico-capable PlatformIO platform. Build from these local snapshots rather
than published Roo dependency versions:

```sh
python3 benchmarks/pico/build.py \
  --platform /path/to/installed/platform-raspberrypi \
  --board waveshare_rp2040_zero
```

The default output is `~/.cache/roo_transport/pico-benchmark/firmware`. The builder
records repository commits, SHA-256 hashes of source snapshots and the UF2 hash
in `manifest.json`. It does not upload or modify the board. Preserve the existing
flash before uploading `firmware.uf2`, then restore it after testing if needed.

With a fresh boot, or `--reset` after a completed run:

```sh
python3 benchmarks/pico/run.py --port /dev/ttyACM0 \
  --baud 8312500 --window-log 5 --application-priority 1 --sender-priority 1 \
  --samples 5 --output results/fast.json \
  --manifest ~/.cache/roo_transport/pico-benchmark/firmware/manifest.json

python3 benchmarks/pico/run.py --port /dev/ttyACM0 --reset \
  --baud 8312500 --window-log 5 --application-priority 1 --sender-priority 1 \
  --samples 2 --fault-us 50000 --output results/recovery.json
```

Use `COM16` or the detected COM port on Windows. This session used a Windows serial
bridge because USB was attached to WSL only for BOOTSEL operations. The portable
runner uses pyserial directly, saves the transcript, and rejects incomplete runs,
byte mismatches, truncated raw transfers, unexpected UART errors and DMA overruns.
A run is accepted only after the DMA lifecycle check passes.

Useful modes are `dma` (default), `arduino`, `original-layout`, `raw-dma`,
`raw-arduino`, `read-cost` and `cpu-cost`. `--peri-clock usb` retains the default
peripheral clock. `--sender-priority` configures the DMA transport sender through
the public API; zero retains its default priority 6. `--application-priority 0`
leaves the main task's original priority unchanged and runs the producer at
priority 1. Neither option changes the receive worker's priority 7. UART pin
assignments are fixed to the example's wiring in `main.cpp`.

The firmware's USB command is:

```text
run BAUD FIFO WINDOW_LOG MODE BYTES SAMPLES SYS_PERI APP_PRIORITY FAULT_US SENDER_PRIORITY
```

`WINDOW_LOG=4/5/6` means 4/8/16 KiB. `MODE=0/1/2/5/7/9/10` maps to the modes above.
`SYS_PERI=1` selects the system clock for `clk_peri`. The tenth parameter,
`SENDER_PRIORITY`, accepts 1–6 for DMA mode; zero or an omitted value retains
the default sender priority 6. `APP_PRIORITY=0` retains the original main-task
priority and producer priority 1. A watchdog resets stalled runs; it does not
turn a partial transfer into a passing measurement.

## Driver lifetime and limits

Construct `ReliableUartDma` after configuring an SDK UART with `uart_init()` and
`gpio_set_function()`. Supply a caller-owned 8 KiB RX buffer aligned to 8 KiB;
construct and destroy all instances on one core, with configuration serialized.
Call `begin()`, connect through `transport()`, and use the resulting `Link`'s
roo_io streams. Keep the producer at least at the sender's priority when maximum
throughput matters. Stop application I/O and destroy Links before `end()`.

Each endpoint owns two DMA channels, one hardware spinlock and an 8 KiB TX ring,
in addition to the caller's RX ring, protocol windows and thread stacks. It shares
`DMA_IRQ_0` with other handlers on the construction core; keep that IRQ enabled
only on that core, including for other DMA users. Clock/baud policy belongs to the
application.
It does not coexist with Arduino SerialUART on the same hardware UART.

This backend currently supports native Arduino-Pico FreeRTOS builds. Host DMA
emulation is not implemented. All twelve shared transport host test targets pass
under the ESP32 profile, and all 38 offline benchmark runner checks pass. Native
Pico server and client example builds passed compilation in the November 5–6
snapshot; the teaching sketch now selects those roles with `kMode`
(`Mode::kServer` or `Mode::kClient`). An earlier Pico FreeRTOS host run exposed
pre-existing ESP32-only dependencies/random-source assumptions in roo_transport's
Bazel setup; those are not part of this hardware-performance change. Real RP2040 hardware is
the validation source for the DMA backend and the stock UART affinity fix.

## Additional hardware checks

The harness checks every payload byte and exercises finite stream shutdown,
worker stop and repeated idle start/stop/resource reuse. A forced 50 ms low input
on GP5 creates a receive break without driving either physical wire; the reliable
protocol retransmits and delivers the complete payload afterward.

The final November 6 diagnostic runs used integer backoff, 8 KiB windows and
application/sender priorities 1/1 at 8.3125 Mbaud. These are diagnostic-harness
rates, separate from the actual example's canonical ten-cycle mean of
4.396369 Mbit/s.

| Diagnostic check | Samples × payload | Payload Mbit/s range | Mean Mbit/s |
| --- | ---: | ---: | ---: |
| Normal transfer | 3 × 1 MiB | 3.755584–3.795070 | 3.768939 |
| Requested 50 ms receive break | 2 × 1 MiB | 3.768885–3.871779 | 3.820332 |

`final-integer-profile-3mib.json` recorded zero DMA overruns and receive errors.
`final-integer-receive-break.json` recorded one receive error on UART1's client
endpoint (`errors2=1`), confirming that the injected break was observed, and
zero overruns on both endpoints. Both runs passed every payload byte, EOF,
worker stop and five idle start/stop/resource-reuse checks. The per-run records
are included in the [November 6 results](results/2026-11-06-minimum.json).

For the DMA counter-renewal regression, build with `--test-rx-count 65536` and run
multi-MiB transfers. This forces many renewals rather than waiting for the normal
32-bit transfer count to expire after roughly 90 minutes at 8 Mbaud. Test firmware
and normal-rate firmware are recorded separately in the results.


The forced-renewal test caught an implementation mistake before this backend was
committed: RP2040 DMA abort clears `TRANS_COUNT`. Treating the post-abort value as
remaining bytes corrupted the software producer position and could stall EOF.
Renewal now samples progress before abort and resolves it against the committed
DMA write address, including ring wrap. Five 1 MiB transfers with a forced 64 KiB
count then passed payload, EOF, stop/reuse, overrun and UART-error checks.

For the published November 5 run, the connected board's complete original 2 MiB
flash was backed up before testing and restored afterward, with a full readback
byte comparison. Raw backup images remain in the local cache and are not part of
this repository.

After the November 5–6 follow-up, the complete original 2 MiB firmware was restored
again and verified by a full on-device readback with byte-for-byte comparison.
The later benchmark images were removed from the board.

Primary implementation references: [Arduino-Pico UART source, 5.4.3](https://github.com/earlephilhower/arduino-pico/blob/5.4.3/cores/rp2040/SerialUART.cpp),
[Arduino-Pico FreeRTOS documentation](https://arduino-pico.readthedocs.io/en/stable/freertos.html),
and [Pico SDK hardware documentation](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html).
