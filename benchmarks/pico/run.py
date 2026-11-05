#!/usr/bin/env python3
"""Run one finite, byte-validated Pico benchmark via a serial console."""
import argparse
import json
import re
import time
from pathlib import Path

import serial

MODES = {"arduino": 0, "raw-arduino": 1, "read-cost": 2,
         "original-layout": 5, "raw-dma": 7, "cpu-cost": 9, "dma": 10}


def fields(line):
    result = {}
    for key, value in re.findall(r"(\w+)=([^ ]+)", line):
        try:
            result[key] = float(value) if "." in value else int(value)
        except ValueError:
            result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--mode", choices=MODES, default="dma")
    parser.add_argument("--baud", type=int, default=8000000)
    parser.add_argument("--fifo", type=int, default=8192)
    parser.add_argument("--window-log", type=int, choices=range(11), default=6)
    parser.add_argument("--bytes", type=int, default=1048576)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--peri-clock", choices=("usb", "sys"), default="sys")
    parser.add_argument("--application-priority", type=int, choices=range(7), default=6)
    parser.add_argument("--sender-priority", type=int, choices=range(7), default=0,
                        help="Sender worker priority; zero retains the library default")
    parser.add_argument("--fault-us", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--reset", action="store_true",
                        help="Reboot firmware waiting for a command or already DONE")
    args = parser.parse_args()
    if args.sender_priority != 0 and args.mode != "dma":
        parser.error("sender priority control requires dma mode")
    if args.bytes <= 0 or args.samples <= 0 or args.fault_us < 0:
        parser.error("byte/sample counts must be positive; fault duration nonnegative")
    if args.fault_us != 0 and (args.mode != "dma" or args.bytes < 65536):
        parser.error("fault injection requires DMA mode and at least 65536 bytes")
    params = [args.baud, args.fifo, args.window_log, MODES[args.mode], args.bytes,
              args.samples, int(args.peri_clock == "sys"), args.application_priority,
              args.fault_us, args.sender_priority]
    if args.reset:
        with serial.Serial(args.port, 115200, timeout=.2) as port:
            port.dtr = True
            port.write(b"reset\n")
        time.sleep(2)
    lines = []
    sent = False
    deadline = time.monotonic() + args.timeout
    with serial.Serial(args.port, 115200, timeout=.2) as port:
        port.dtr = True
        while time.monotonic() < deadline:
            line = port.readline().decode(errors="replace").strip()
            if not line:
                continue
            print(line, flush=True)
            lines.append(line)
            if line.startswith("READY") and not sent:
                port.write(("run " + " ".join(map(str, params)) + "\n").encode())
                sent = True
            if line == "DONE":
                break
    record = {"parameters": vars(args).copy(), "rows": [], "passed": False}
    record["parameters"] = {k: str(v) if isinstance(v, Path) else v
                            for k, v in record["parameters"].items()}
    for line in lines:
        if line.startswith(("CONFIG", "RESULT", "LATENCY", "DMA", "LIFECYCLE")):
            record["rows"].append({"record": line.split()[0], **fields(line)})
    results = [row for row in record["rows"] if row["record"] == "RESULT"]
    valid = sent and "DONE" in lines and bool(results)
    config = next((row for row in record["rows"] if row["record"] == "CONFIG"), {})
    expected = {"baud": args.baud, "mode": MODES[args.mode], "window": args.window_log,
                "bytes": args.bytes, "repeats": args.samples,
                "app_prio": args.application_priority, "fault_us": args.fault_us}
    valid &= all(config.get(key) == value for key, value in expected.items())
    valid &= config.get("sender_prio", 0) == args.sender_priority
    if args.mode not in ("cpu-cost", "read-cost"):
        valid &= len(results) == args.samples
        valid &= all(row.get("bad") == 0 and row.get("bytes") == args.bytes
                     and row.get("overflow", 0) == 0 for row in results)
    else:
        valid &= all(row.get("bad", 0) == 0 for row in results)
    if args.mode == "dma":
        valid &= "LIFECYCLE idle_stop_and_reuse=PASS" in lines
        dma = next((row for row in record["rows"] if row["record"] == "DMA"), {})
        valid &= dma.get("overruns1") == 0 and dma.get("overruns2") == 0
        if args.fault_us == 0:
            valid &= dma.get("errors1") == 0 and dma.get("errors2") == 0
    if args.manifest:
        record["firmware"] = json.loads(args.manifest.read_text())
    record["passed"] = bool(valid)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + "\n")
    args.output.with_suffix(".log").write_text("\n".join(lines) + "\n")
    if not valid:
        raise SystemExit("Benchmark incomplete or validation failed; see saved log")


if __name__ == "__main__":
    main()
