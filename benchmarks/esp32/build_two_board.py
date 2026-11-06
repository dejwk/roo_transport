#!/usr/bin/env python3
"""Build finite ESP32 server/client benchmarks from the illustrative sketch."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

WINDOWS = ("256B", "512B", "1KB", "2KB", "4KB", "8KB", "16KB",
           "32KB", "64KB", "128KB", "256KB")


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("Example changed; expected exactly one: " + old.splitlines()[0])
    return source.replace(old, new)


def make_source(source, role, config):
    """Adapt only settings and console reporting; retain the actual benchmark."""
    source = replace_once(source, "#include <Arduino.h>",
                          "#include <Arduino.h>\n#include <driver/uart.h>")
    replacements = {
        "static constexpr Mode kMode = Mode::kLoopback;":
            "static constexpr Mode kMode = Mode::k" + role.capitalize() + ";",
        "static constexpr uint32_t kRuns = 0;":
            "static constexpr uint32_t kRuns = " + str(config["runs"]) + ";",
        "static constexpr uint32_t kPayloadBytes = 1024 * 1024;":
            "static constexpr uint32_t kPayloadBytes = " + str(config["bytes"]) + ";",
        "static constexpr size_t kLatencySamples = 100;":
            "static constexpr size_t kLatencySamples = " + str(config["samples"]) + ";",
        "static constexpr size_t kChunkSize = 256;":
            "static constexpr size_t kChunkSize = " + str(config["chunk"]) + ";",
        "static constexpr int kIoPriority = 1;":
            "static constexpr int kIoPriority = " + str(config["io_priority"]) + ";",
        "static constexpr bool kWaitForSerial = true;":
            "static constexpr bool kWaitForSerial = false;",
        "static constexpr int kPinClientTx = 17;":
            "static constexpr int kPinClientTx = 4;",
        "static constexpr int kPinClientRx = 16;":
            "static constexpr int kPinClientRx = 2;",
        "static constexpr uint32_t kBaud = 5000000;":
            "static constexpr uint32_t kBaud = " + str(config["baud"]) + ";",
        "    roo_transport::kBufferSize2KB;":
            "    roo_transport::kBufferSize" + WINDOWS[config["window_log"]] + ";",
    }
    for old, new in replacements.items():
        source = replace_once(source, old, new)

    start = source.index('  Serial.printf(\n      "Round-trip times [ms]')
    end = source.index('\n}', start)
    latency = '''  Serial.printf(
      "LATENCY role=client min_ms=%f p50_ms=%f p90_ms=%f p99_ms=%f max_ms=%f\\n",
      rtt[0], rtt[kLatencySamples / 2], rtt[kLatencySamples * 9 / 10],
      rtt[kLatencySamples * 99 / 100], rtt[kLatencySamples - 1]);'''
    source = source[:start] + latency + source[end:]

    start = source.index('  Serial.printf("Throughput for ')
    end = source.index('\n}', start)
    throughput = '''  static uint32_t sample = 0;
  Serial.printf("RESULT role=client sample=%lu bytes=%lu us=%llu mbps=%.6f bad=0\\n",
                static_cast<unsigned long>(sample++),
                static_cast<unsigned long>(payload_bytes),
                static_cast<unsigned long long>(elapsed),
                payload_bytes * 8.0 / elapsed);'''
    source = source[:start] + throughput + source[end:]

    uart = "UART_NUM_1" if role == "server" else "UART_NUM_2"
    config_print = '''  uint32_t actual_baud = 0;
  CHECK_EQ(uart_get_baudrate(UART_PORT, &actual_baud), ESP_OK);
  TaskHandle_t sender_task = xTaskGetHandle("ROLE_NAME-send");
  CHECK(sender_task != nullptr);
  Serial.printf("SYSTEM role=ROLE chip=%s revision=%u sdk=%s cpu=%lu apb=%lu\\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getSdkVersion(),
                static_cast<unsigned long>(getCpuFrequencyMhz() * 1000000),
                static_cast<unsigned long>(getApbFrequency()));
  Serial.printf(
      "CONFIG role=ROLE baud=%lu actual=%lu tx=4 rx=2 window=WINDOW_LOG "
      "bytes=%lu runs=%lu latency_samples=%u chunk=%u app_prio=%u sender_prio=%u "
      "rxbuf=256 txbuf=1024 rx_fifo=64 rx_timeout=1\\n",
      static_cast<unsigned long>(kBaud), static_cast<unsigned long>(actual_baud),
      static_cast<unsigned long>(kPayloadBytes), static_cast<unsigned long>(kRuns),
      static_cast<unsigned>(kLatencySamples), static_cast<unsigned>(kChunkSize),
      static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
      static_cast<unsigned>(uxTaskPriorityGet(sender_task)));'''
    config_print = config_print.replace("UART_PORT", uart).replace(
        "ROLE_NAME", "srv" if role == "server" else "cli").replace(
        "ROLE", role).replace("WINDOW_LOG", str(config["window_log"]))
    source = replace_once(source, "  InitializeEndpoints();",
                          "  InitializeEndpoints();\n" + config_print)
    run = '''  Serial.println("READY role=ROLE");
  while (Serial.read() != 'r') delay(1);
  RunBenchmark(kRuns, kPayloadBytes);
  Serial.println("LIFECYCLE role=ROLE stream_eof_and_stop=PASS");
  Serial.println("DONE role=ROLE");'''.replace("ROLE", role)
    return replace_once(source, "  RunBenchmark(kRuns, kPayloadBytes);", run)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", default="espressif32")
    parser.add_argument("--board", default="esp32dev")
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path,
                        default=Path.home() / ".cache/roo_transport/esp32-two-board/firmware")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--bytes", type=int, default=1048576)
    parser.add_argument("--samples", type=int, default=100,
                        help="One-byte latency samples in each batch")
    parser.add_argument("--baud", type=int, default=5000000)
    parser.add_argument("--window-log", type=int, choices=range(11), default=3)
    parser.add_argument("--chunk", type=int, choices=range(256, 4097, 256), default=256)
    parser.add_argument("--io-priority", type=int, choices=range(1, 24), default=1)
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    if min(args.runs, args.bytes, args.samples, args.baud, args.jobs) <= 0:
        parser.error("Counts, baud and jobs must be positive")
    if max(args.runs, args.bytes, args.baud) > 0xffffffff or args.samples > 4096:
        parser.error("Runs/payload/baud must fit uint32_t; samples must be <=4096")
    output = args.output.expanduser().resolve()
    source_dir = output / "generated"
    source_dir.mkdir(parents=True, exist_ok=True)
    here = Path(__file__).resolve().parent
    original = here.parents[1] / "examples/ReliableSerial/Benchmark/Benchmark.ino"
    source = original.read_text()
    config = {key: getattr(args, key) for key in (
        "runs", "bytes", "samples", "baud", "window_log", "chunk", "io_priority")}
    pair = {"configuration": config, "example_sha256": hashlib.sha256(
        source.encode()).hexdigest(), "firmware": {}}
    for role in ("server", "client"):
        sketch = source_dir / (role + ".cpp")
        sketch.write_text(make_source(source, role, config))
        print("Generated", role, sketch, flush=True)
        if not args.generate_only:
            subprocess.run([sys.executable, str(here / "build.py"),
                            "--platform", args.platform, "--board", args.board,
                            "--pio", args.pio, "--jobs", str(args.jobs),
                            "--output", str(output / role), "--sketch", str(sketch)],
                           check=True)
            pair["firmware"][role] = json.loads(
                (output / role / "manifest.json").read_text())
    (output / "pair-manifest.json").write_text(json.dumps(pair, indent=2) + "\n")
    print("Pair manifest:", output / "pair-manifest.json")


if __name__ == "__main__":
    main()
