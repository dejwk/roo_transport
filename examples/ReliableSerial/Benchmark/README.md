# Reliable serial benchmark

This example shows the application setup needed for bulk transfer rates. Updating
the transport library alone does not configure UART rings, FIFO thresholds or task
priorities. The same request/response code uses `roo_transport::Link` and roo_io
streams on all supported branches.

## Platform selection and wiring

| Build | Transport selected | Crossed pins |
| --- | --- | --- |
| ESP32 Arduino | `ReliableSerial1/2`, using native IDF UART streams | TX4 → RX16, TX17 → RX2 |
| Native RP2040 Arduino-Pico FreeRTOS | `pico::ReliableUartDma` | TX12 → RX5, TX4 → RX13 |
| Other Arduino-Pico builds/emulation | Existing Arduino serial adapter | Same Pico pins |

Automatic DMA selection requires `ARDUINO_ARCH_RP2040`, `PICO_RP2040 != 0`,
`__FREERTOS`, and no `ROO_TESTING`. RP2350 and emulated builds do not silently enter
the unvalidated RP2040 DMA path. Native Arduino-Pico needs FreeRTOS for this
threaded example; bare-metal builds produce an explicit requirement error.

Both active UARTs and transports are initialized serially in `setup()` before
starting application workers. The ESP32 UART interrupts therefore remain on the
setup core. Pico DMA resources are constructed, stopped and released on that same
core, as required by the driver. `MODE_SERVER` initializes only the server UART;
`MODE_CLIENT` initializes only the client UART.

The ESP32 fake-board fixture follows these new ESP32 pin choices. `ROO_TESTING`
retains Arduino adapters rather than selecting DMA. The existing Bazel example
and finite integration test target use the ESP32 fixture. Pico-profile host
emulation is not validated here; its existing platform routing and fake UART
fixture need separate work.

## Bulk-transfer configuration

The example gives every choice a compile-time override. Prefix the suffixes in
this table with `ROO_TRANSPORT_BENCHMARK_`:

| Suffix | ESP32 default | Native RP2040 DMA default |
| --- | ---: | ---: |
| `BAUD` | 5,000,000 | 8,312,500 |
| `WINDOW_LOG2` | 3: nominal 2 KiB per direction | 5: nominal 8 KiB per direction |
| `RX_BUFFER` | 256 bytes (Arduino default) | Fixed caller-owned aligned 8 KiB RX ring |
| `TX_BUFFER` | 1024 bytes | Fixed 8 KiB DMA TX ring |
| `RX_FIFO` | 64 bytes | Not an Arduino FIFO configuration |
| `RX_TIMEOUT` | 1 symbol | Not an Arduino receive-timeout configuration |
| `IO_PRIORITY` | 1 | 1 |
| `SENDER_PRIORITY` | 1 | 1 |

`WINDOW_LOG2` counts 256-byte protocol slots, so `3/4/5/6` mean 2/4/8/16 KiB.
Smaller windows save RAM; they also need to cover the amount
of data in flight until acknowledgments arrive. RX and TX buffer macros configure
Arduino UART rings on ESP32; they do not resize the native DMA driver's rings.
The legacy Pico Arduino fallback uses `RX_BUFFER` for `SerialUART`'s software FIFO
(default 1024), 115200 baud and a 4 KiB protocol window.

ESP32 configures the RX/TX software rings before `begin()`, then sets the hardware
RX FIFO threshold and timeout before starting the reliable transport. The lower
FIFO threshold gives the ISR more headroom before hardware overflow at 5 Mbaud.
Larger software buffers cannot recover bytes already lost in the hardware FIFO.

Sender attributes configure **only the transport send worker**. The complete
attributes are copied, with no default merging; specify the 4096-byte stack
explicitly to retain that budget when changing priority or name:

```cpp
roo::thread::attributes sender;
sender.set_name("srv-send");
sender.set_stack_size(4096);
sender.set_priority(1);
reliable_serial.begin(sender);
```

The example uses the same priority for bulk producer and consumer. It leaves the
ESP32 UART event task and the Pico receive worker at their existing priorities.
Matching producer/sender priorities matters: lowering only the producer while
leaving a high-priority sender can prevent useful batching. `IO_PRIORITY=0`
preserves the main task's priority and uses priority 1 for the loopback server.
`SENDER_PRIORITY` applies to the ESP32 and native Pico DMA paths; the legacy Pico
Arduino fallback retains its original sender initialization.

The ESP32 SDK UART event task has its own stack, separate from the send worker.
The hardware build helper supplies:

```text
-DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=4096
```

Use that core build flag when compiling this example outside the helper as well.
A sketch-local `#define` does not change a separately compiled Arduino core.

Native RP2040 DMA initialization explicitly derives `clk_peri` from `clk_sys`
before either UART is configured; it does not overclock the CPU. This avoids the
48 MHz peripheral-clock ceiling that otherwise limits UART operation to 3 Mbaud.
Other peripherals in a larger application must be configured for that clock
policy. The example supplies aligned 8 KiB RX rings and never calls
`Serial1/2.begin()` on UARTs owned by DMA. Keep other `DMA_IRQ_0` users on the same
construction core.

These defaults demonstrate a bulk workload, not a universal application policy.
Radio traffic, other tasks, wiring, board/core versions and different message
sizes change the achievable rate. The library's UART backlog-draining fix applies
automatically, but does not make every configuration equally fast. See the
[ESP32 hardware measurements](../../../benchmarks/esp32/README.md) and
[RP2040 hardware measurements](../../../benchmarks/pico/README.md) for the measured
recipes and their limits. The Pico default uses the practical 8 KiB window,
priority-1 profile below; 16 KiB windows and priorities 6/6 remain the peak-tested
profile rather than the default.

## Measured example rates and resource probes

The same example now selects native RP2040 DMA automatically on the supported
FreeRTOS build described above. General `ReliableSerial1/2` users keep their
Arduino backend; automatic DMA selection belongs to this example's platform
branch.

The November 5–6 follow-up measured this standalone example directly; diagnostic
harness rates belong to their own workload and source snapshot:

| Board and source | Windows; UART buffers | App / sender priority | Samples | Useful Mbit/s |
| --- | --- | --- | ---: | ---: |
| ESP32, before integer backoff | 2 KiB; RX256 / TX1024 | 1 / 1 | 3 × 1 MiB | 3.537897–3.561922 |
| ESP32, integer backoff | 2 KiB; RX256 / TX1024 | 1 / 1 | 3 × 1 MiB | **3.802957–3.827351** |
| ESP32 canonical default, integer backoff | 2 KiB; RX256 / TX1024 | 1 / 1 | 10 × 1 MiB | **3.813356–3.827941** |
| RP2040 DMA, before integer backoff | 16 KiB; RX8 KiB / TX8 KiB DMA | 6 / 6 | 3 × 1 MiB | 3.185256–3.239179 |
| RP2040 DMA, integer backoff, peak tested | 16 KiB; RX8 KiB / TX8 KiB DMA | 6 / 6 | 3 × 1 MiB | **4.654853–4.743518** |
| RP2040 DMA canonical default, integer backoff | 8 KiB; RX8 KiB / TX8 KiB DMA | 1 / 1 | 10 × 1 MiB | **4.333691–4.468476** |

The three-cycle before/after pairs used identical sketch source. The older library source matches the
pre-integer `23d2691` tree; newer firmware used the measured integer-backoff source
snapshot. Later resource screens also include sender-attribute validation guards;
the recorded hashes identify each actual source snapshot. The integer policy is
committed in `2bc8cd4`, after the sender-attribute guards in `d2d63ad`. Build
repository HEADs can predate pending source changes, so consult
the firmware/source fingerprints in the [ESP32 records](../../../benchmarks/esp32/results/2026-11-06-minimum.json)
and [Pico records](../../../benchmarks/pico/results/2026-11-06-minimum.json).
All rows completed byte validation and finite shutdown. The separate diagnostic
program's watchdog/progress work and latency sequence differ, and instrumented
runtime-profile variants are not minimum-resource recipes.

The final canonical defaults each completed ten 1 MiB cycles with exact payloads,
EOF, worker shutdown and `DONE`: ESP32 averaged 3.818552 Mbit/s and Pico averaged
4.396369 Mbit/s. All 12 transport host test targets and 38 offline runner checks
pass. Native ESP32 and Pico server/client modes and the standard-C++
guarded-header smoke check also compile successfully. The diagnostic harness
additionally compiles against legacy ESP32 transport source `74564ea`, retaining
the legacy sender defaults when its newer attribute API is absent.

Retry expiration is computed for every transmitted data packet under the
transmitter mutex. Replacing `pow()` and floating-point jitter removes work from
that path. The shared integer helper grows the base by 1.33 with nearest-microsecond
rounding, caps it before ±20% jitter, and maps the actual RNG range to 8192 bins
with at most 49 µs spacing. Correct normalization also fixes the old full-width
hardware RNG / `RAND_MAX` mismatch, which could expand the upper jitter to +60%.
The [ESP32 analysis](../../../benchmarks/esp32/README.md#retry-computation-and-receive-work)
explains the lock scope, floating-point affinity risk and limits of the profiling
comparison; the CPU and scheduling effects were not isolated.

The integer helper improved the controlled standalone-example pairs above, but
not every workload. At the same nominal ESP32 low-resource settings, the current
diagnostic harness averaged 3.690453 Mbit/s over three 1 MiB samples, below the
pre-integer diagnostic's 3.853364 Mbit/s. Its extra progress/watchdog work and
latency sequence differ from this example. Use the current example's measured
rate when selecting this recipe; the historical diagnostic rate is not a promise
for the current source.

The final diagnostic checks use the current integer source and the practical
settings for each board. They remain separate from the canonical example rates:

| Diagnostic board | Normal, 3 × 1 MiB | Forced 50 ms receive break, 2 × 1 MiB |
| --- | ---: | ---: |
| ESP32 | 3.687678–3.694374 Mbit/s | 3.578510–3.683906 Mbit/s |
| RP2040 DMA | 3.755584–3.795070 Mbit/s | 3.768885–3.871779 Mbit/s |

Both recovered with complete byte-checked payloads, EOF and worker shutdown.
ESP32 confirmed the fault was applied and its input routing restored. Pico
recorded the injected receive error, zero DMA overruns and five idle stop/reuse
checks. These checks validate recovery; the fault rates are not tuning targets.

The default table above remains the example's current tested configuration.
Smaller independent ESP32 buffer screens passed, and RX256/TX576 with integer
backoff reached 3.802650–3.807706 Mbit/s over three 1 MiB samples. That probe does
not replace the default TX1024 or qualify every combination of smaller rings and
windows. The integer ESP32 1 KiB-window screen passed correctness at
3.648584–3.662914 Mbit/s; the practical default remains 2 KiB. The integer-backoff
Pico screens support the following resource tradeoffs:

| Native RP2040 profile | Nominal windows per direction | App / sender priority | Useful Mbit/s, 3 × 1 MiB |
| --- | --- | --- | ---: |
| Peak tested | 16 KiB | 6 / 6 | 4.654853–4.743518 |
| Lower priority, larger window | 16 KiB | 1 / 1 | 4.552240–4.618967 |
| Practical default, initial screen | 8 KiB | 1 / 1 | **4.379166–4.390346** |
| Lower memory | 4 KiB | 1 / 1 | 3.814649–3.871041 |
| Smaller-window screen | 2 KiB | 1 / 1 | 3.022296–3.039832 |

All rows completed byte validation and finite shutdown. The 8 KiB / priority-1
profile is the practical default; it trades some peak rate for smaller windows
and lower application/sender priorities. Its final ten-cycle confirmation reached
4.333691–4.468476 Mbit/s, mean 4.396369 Mbit/s. The other rows remain three-cycle
screens; recovery passed with the separate diagnostic harness above. Fixed 8 KiB
RX and TX DMA rings remain per endpoint in every row, and the receive worker stays at
priority 7. Nominal protocol windows are also per endpoint; metadata, SDK queues
and worker stacks require additional RAM. These settings do not establish global
hardware minima.

After validation, both boards' complete original firmwares were restored and
verified: the ESP32's full 4 MiB on-device digest matched, and the Pico's full
2 MiB readback matched byte for byte. Flash images and backups remain outside
the repository.

## Modes and finite smoke runs

`MODE` defaults to `MODE_LOOPBACK`. Override it with `-DMODE=MODE_SERVER` or
`-DMODE=MODE_CLIENT` when using separate boards; connect TX to peer RX and share
ground. Both sides must agree on baud and wiring. Different latency sample counts
are fine because the server responds to lengths until the client closes its
request stream.

Additional overrides are:

| Suffix | Default | Meaning |
| --- | ---: | --- |
| `RUNS` | 0 | Continuous; a positive value runs that many latency/bulk cycles |
| `LATENCY_SAMPLES` | 100 | One-byte request/response samples per cycle |
| `PAYLOAD_BYTES` | 1 MiB on fast paths | Validated bulk bytes per cycle |
| `WAIT_FOR_SERIAL` | 1 on native Pico DMA, otherwise 0 | Wait for the console before running |

The legacy Pico Arduino fallback uses `BAUD / 5` payload bytes by default to keep
its slower test bounded. Set `WAIT_FOR_SERIAL=0` for a headless native Pico peer.

For a short native ESP32 smoke build from sibling Roo checkouts:

```sh
python3 benchmarks/esp32/build.py --platform /path/to/installed/espressif32 \
  --output ~/.cache/roo_transport/esp32-benchmark/example \
  --sketch examples/ReliableSerial/Benchmark/Benchmark.ino \
  --extra-flag=-DROO_TRANSPORT_BENCHMARK_RUNS=1 \
  --extra-flag=-DROO_TRANSPORT_BENCHMARK_LATENCY_SAMPLES=5 \
  --extra-flag=-DROO_TRANSPORT_BENCHMARK_PAYLOAD_BYTES=65536
```

The Pico helper accepts the same `--sketch` and `--extra-flag` options. Use a
separate output directory, such as `~/.cache/roo_transport/pico-benchmark/example`,
to retain the diagnostic firmware project. Building does not upload. This standalone example starts after reset/console readiness;
it does not use the diagnostic harness's `run ...` command protocol.

Each bulk interval validates every delivered byte and prints timing only after
the interval ends. Packet counters are explicitly labeled by their local endpoint;
their differences are diagnostics rather than an exact wire-loss estimate.

A finite client closes its request output. The server observes EOF and closes its
response output, then the client verifies response EOF. Readers and writers stay
inside the lifetime of their `Link`. Native DMA and ESP32 endpoints stop after
application I/O has ended; the loopback joins the server before releasing either
transport. `DONE` marks completion. The legacy Pico Arduino adapter currently
cannot stop an idle receive worker, so a finite fallback workload leaves that
process-lifetime adapter alive until reset rather than adding a timing-dependent
wakeup workaround.

`Loopback` remains an intentionally throttled small-message demonstration. Its
per-message flush and delay are useful for that example, but not a bulk-throughput
recipe; use this benchmark to study buffer and scheduling choices.
