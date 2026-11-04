# RP2040 UART throughput and hardware validation

The RP2040-Zero with crossed UARTs reached **3.92 Mbit/s of reliable application
payload** (3.87 Mbit/s mean over ten consecutive 1 MiB transfers), with the CPU at its normal
133 MHz. Both protocol endpoints ran on this one board. Every delivered byte was
checked against a known pattern; throughput excludes UART framing, packet headers,
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
- Fast configuration: actual **8,312,500 baud**, 8N1; peripheral clock explicitly
  derived from `clk_sys` at 133 MHz; 16 KiB send/receive protocol windows;
  8 KiB RX and TX DMA rings per endpoint; both application tasks at priority 6.
  The receive worker runs at priority 7, the sender at priority 6.

The benchmark changes `clk_peri` before initializing UARTs. The library itself
leaves clocks and baud under application control. Other peripherals using that
clock must be configured accordingly. The CPU is not overclocked.

## Measurements

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
`-Os`; increased instruction-cache pressure is a plausible explanation, not a
measured cache profile. A CPU microbenchmark found an uncontended mutex pair cost
about 19 µs and a 248-byte Murmur hash about 19 µs. Raising baud without reducing
software work therefore could not solve the original problem.

The remaining ceiling is software/CPU scheduling on a two-endpoint loopback, not
a demonstrated wire limit. The existing ESP32 3.39 Mbit/s reference used **two
separate ESP32s**. A two-Pico measurement is still needed to establish its maximum;
this one-board result is already in the requested 3–4 Mbit/s range.

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
  --baud 8312500 --samples 5 --output results/fast.json \
  --manifest ~/.cache/roo_transport/pico-benchmark/firmware/manifest.json

python3 benchmarks/pico/run.py --port /dev/ttyACM0 --reset \
  --samples 2 --fault-us 50000 --output results/recovery.json
```

Use `COM16` or the detected COM port on Windows. This session used a Windows serial
bridge because USB was attached to WSL only for BOOTSEL operations. The portable
runner uses pyserial directly, saves the transcript, and rejects incomplete runs,
byte mismatches, truncated raw transfers, unexpected UART errors and DMA overruns.
A run is accepted only after the DMA lifecycle check passes.

Useful modes are `dma` (default), `arduino`, `original-layout`, `raw-dma`,
`raw-arduino`, `read-cost` and `cpu-cost`. `--peri-clock usb` retains the default
peripheral clock; `--application-priority 0` retains the original application
priorities. UART pin assignments are fixed to the example's wiring in `main.cpp`.

The firmware's USB command is:

```text
run BAUD FIFO WINDOW_LOG MODE BYTES SAMPLES SYS_PERI APP_PRIORITY FAULT_US
```

`WINDOW_LOG=4/5/6` means 4/8/16 KiB. `MODE=0/1/2/5/7/9/10` maps to the modes above.
`SYS_PERI=1` selects the system clock for `clk_peri`. A watchdog resets stalled
runs; it does not turn a partial transfer into a passing measurement.

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
emulation is not implemented. The eight shared transport host test binaries pass
under the ESP32 profile. An attempted Pico FreeRTOS host run exposed pre-existing
ESP32-only dependencies/random-source assumptions in roo_transport's Bazel setup;
those are not part of this hardware-performance change. Real RP2040 hardware is
the validation source for the DMA backend and the stock UART affinity fix.

## Additional hardware checks

The harness checks every payload byte and exercises finite stream shutdown,
worker stop and repeated idle start/stop/resource reuse. A forced 50 ms low input
on GP5 creates a receive break without driving either physical wire; the reliable
protocol retransmits and delivers the complete payload afterward.

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

The connected board's complete original 2 MiB flash was backed up before testing
and restored afterward, with a full readback byte comparison. Raw backup images
remain in the local cache and are not part of this repository.

Primary implementation references: [Arduino-Pico UART source, 5.4.3](https://github.com/earlephilhower/arduino-pico/blob/5.4.3/cores/rp2040/SerialUART.cpp),
[Arduino-Pico FreeRTOS documentation](https://arduino-pico.readthedocs.io/en/stable/freertos.html),
and [Pico SDK hardware documentation](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html).
