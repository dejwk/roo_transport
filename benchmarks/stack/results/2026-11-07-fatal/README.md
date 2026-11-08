# ESP32 fatal-path stack validation

An intentional peer-reset FATAL runs on the real UART receive task. With
2,048-byte sender/receive stacks, both ESP32 revisions reboot after a double
exception during core-dump generation and leave no valid dump. With 4,096-byte
stacks, both reboot with a validated 18,148-byte flash ELF dump.

The [result record](../2026-11-07-fatal-validation.json) links all four console
transcripts and both build manifests. Arduino-ESP32 3.3.7 uses ESP-IDF package
`5.5.0+sha.87912cd291`, with flash ELF/CRC32 dumps enabled and
`CONFIG_ESP_COREDUMP_STACK_SIZE=0`. This configuration uses the crashing worker's
stack for dump generation.

## Method and scope

Each board uses its two real UARTs through the internal GPIO input matrix:
UART1 TX4 feeds UART2 RX, and UART2 TX17 feeds UART1 RX. The physically connected
second board is not the active peer. Both builds contain identical application
and Roo source bytes; only sender and UART event stack reservations differ.
The application task has 8 KiB. Detailed Roo connection logging is enabled;
global Arduino/ESP-IDF verbose logging is disabled.

The probe establishes a connection, removes the peer transport, then injects
checksum-valid control frames to exercise warning logging. A checksum-valid
initial handshake with a new stream ID triggers peer-disconnect handling and
its registered FATAL callback on the DUT receive worker. The callback mirrors
the `connectOrDie()` fatal policy and adds a probe label. It does not simulate
a physical power cycle of the peer or run bulk transfers.

Before triggering, the probe requires `esp_core_dump_image_erase()` to succeed.
An RTC noinit cookie identifies the ensuing panic reboot. After reboot,
`esp_core_dump_image_check()` validates dump integrity and
`esp_core_dump_image_get()` returns image address/size. A console backtrace
alone does not count as a valid core dump.

The 2 KiB console output includes `Double exception` and
`Re-entered core dump! Exception happened during core dump!`. Both post-boot
APIs return `ESP_ERR_INVALID_SIZE` (`0x104`, decimal 260). With 4 KiB they return
`ESP_OK`, address `0x3f0000`, size 18,148 bytes, without dump reentry.

This is one fatal observation per stack size per board revision. There is no
explicit stack-canary report or fatal-time stack watermark. Pre-fatal watermarks
cover handshake/warning work only. Successful dumping does not establish an
upper bound for every panic path. The separate
[normal-workload record](../2026-11-07-stack-validation.json) covers streaming,
small I/O, EOF, shutdown and Pico diagnostics; Pico fatal handling is not tested
here.

## Reproduction

[probe-source.txt](probe-source.txt) preserves the literal C++ build input as a
text artifact, including the line numbers reported in the console. The builder
copies it to `src/main.cpp`. Keep the Roo repositories as sibling checkouts and
use the recorded SDK/source snapshots for a matched comparison.

Build the two configurations sequentially from the repository root:

```sh
python3 benchmarks/esp32/build.py \
  --platform /path/to/installed/espressif32 \
  --sketch benchmarks/stack/results/2026-11-07-fatal/probe-source.txt \
  --output ~/.cache/roo_transport/fatal-2048 \
  --extra-flag=-DSTACK_BYTES=2048 \
  --extra-flag=-DMLOG_roo_transport_reliable_channel_connection=1 \
  --extra-flag=-UARDUINO_SERIAL_EVENT_TASK_STACK_SIZE \
  --extra-flag=-DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=2048

python3 benchmarks/esp32/build.py \
  --platform /path/to/installed/espressif32 \
  --sketch benchmarks/stack/results/2026-11-07-fatal/probe-source.txt \
  --output ~/.cache/roo_transport/fatal-4096 \
  --extra-flag=-DSTACK_BYTES=4096 \
  --extra-flag=-DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=4096 \
  --extra-flag=-DMLOG_roo_transport_reliable_channel_connection=1
```

The builder follows the Arduino core's 2,048 B receive-stack default; the
4 KiB control sets its override explicitly. This archived probe uses its own
`STACK_BYTES` for sender attributes and asserts that its sender and receive
stack settings match at compile time.

Flash each configuration onto each board, using the
[ESP32 flashing precautions](../../../esp32/README.md#reproduce-the-two-board-benchmark)
for crossed GPIO2/GPIO4 wiring. Open the USB console at 115,200 baud, reset,
wait for `READY fatal_probe=1`, then send:

```text
run 3 5000000 3 1048577 1
```

The final two fields are inherited command placeholders; this fatal probe does
not send their bulk payload. Capture through `EXPECTED_FATAL`, the panic, the
post-reboot `COREDUMP_BOOT` and the next `READY`. The post-boot record must have
`post_fatal=1`, matching cookies, `reset_reason=4` and the expected stack size.
A valid dump requires `check=0`, `image=0` and a nonzero size. The probe waits
for another command after reboot and does not automatically repeat the crash.
