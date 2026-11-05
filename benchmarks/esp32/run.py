#!/usr/bin/env python3
"""Run one finite, byte-validated ESP32 UART benchmark over its console."""
import argparse
import json
import re
import time
from pathlib import Path

import serial

MODES = {"reliable": 0, "raw-arduino": 1, "raw-idf": 2,
         "raw-idf-reverse": 3}


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
    parser.add_argument("--mode", choices=MODES, default="reliable")
    parser.add_argument("--baud", type=int, default=5000000)
    parser.add_argument("--rx-buffer", type=int, default=8192)
    parser.add_argument("--tx-buffer", type=int, default=1024)
    parser.add_argument("--window-log", type=int, choices=range(11), default=6)
    parser.add_argument("--bytes", type=int, default=1048576)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--application-priority", type=int, choices=range(24), default=23)
    parser.add_argument("--rx-fifo", type=int, choices=range(1, 128), default=64)
    parser.add_argument("--rx-timeout", type=int, choices=range(102), default=1)
    parser.add_argument("--chunk", type=int, choices=range(256, 4097, 256), default=256)
    parser.add_argument("--split-irq", action="store_true",
                        help="Install UART1 IRQ on core0 and UART2 on the application core")
    parser.add_argument("--fault-us", type=int, default=0,
                        help="Force UART2 RX low internally after 32768 delivered bytes")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--reset", action="store_true",
                        help="Reset the ESP32 through the USB-UART RTS/EN control line")
    args = parser.parse_args()
    if args.bytes <= 0 or args.samples <= 0 or args.rx_buffer < 256 or args.tx_buffer < 0:
        parser.error("positive byte/sample counts and RX>=256, TX>=0 are required")
    if args.fault_us < 0 or (args.fault_us != 0 and
                            (args.mode != "reliable" or args.bytes < 65536)):
        parser.error("fault injection requires reliable mode, bytes>=65536, fault-us>=0")
    params = [args.baud, args.rx_buffer, args.tx_buffer, args.window_log,
              MODES[args.mode], args.bytes, args.samples,
              args.application_priority, args.rx_fifo, args.rx_timeout, args.chunk,
              int(args.split_irq), args.fault_us]
    lines = []
    sent = False
    deadline = time.monotonic() + args.timeout
    # Release both USB-UART control lines before opening; the console itself
    # does not require DTR and ESP32 bridge boards use these lines for reset.
    port = serial.Serial(timeout=.2)
    port.port = args.port
    port.baudrate = 115200
    port.dtr = False
    port.rts = False
    with port:
        if args.reset:
            # Hold EN low while discarding any output from an open-time reset.
            # Keeping GPIO0 released boots the application, not the ROM loader.
            port.rts = True
            time.sleep(.1)
            port.reset_input_buffer()
            port.rts = False
        # Keep this port open through reset so the early READY line is retained.
        while time.monotonic() < deadline:
            line = port.readline().decode(errors="replace").strip()
            if not line:
                continue
            print(line, flush=True)
            lines.append(line)
            if line.startswith("READY") and not sent:
                port.write(("run " + " ".join(map(str, params)) + "\n").encode())
                sent = True
            if ((sent and line == "DONE") or line.startswith("ERROR ")
                    or "Check failed:" in line or line.startswith("abort()")):
                break
            if line.startswith("RESULT ") and fields(line).get("bad", 0) != 0:
                break
    record = {"parameters": vars(args).copy(), "rows": [], "passed": False}
    record["parameters"] = {k: str(v) if isinstance(v, Path) else v
                            for k, v in record["parameters"].items()}
    for line in lines:
        if line.startswith(("SYSTEM", "CONFIG", "RESULT", "LATENCY", "STARTUP_ERRORS", "ERRORS", "TRANSFER_ERRORS", "MISMATCH", "ERROR_TYPES", "FAULT", "LIFECYCLE")):
            record["rows"].append({"record": line.split()[0], **fields(line)})
    results = [row for row in record["rows"] if row["record"] == "RESULT"]
    valid = sent and "DONE" in lines and len(results) == args.samples
    config = next((row for row in record["rows"] if row["record"] == "CONFIG"), {})
    expected = {"baud": args.baud, "rxbuf": args.rx_buffer, "txbuf": args.tx_buffer,
                "mode": MODES[args.mode], "window": args.window_log,
                "bytes": args.bytes, "repeats": args.samples,
                "app_prio": args.application_priority, "rx_fifo": args.rx_fifo,
                "rx_timeout": args.rx_timeout, "chunk": args.chunk,
                "split_irq": int(args.split_irq), "fault_us": args.fault_us}
    valid &= all(config.get(key) == value for key, value in expected.items())
    valid &= config.get("actual", 0) > 0 and config.get("actual2", 0) > 0
    valid &= all(row.get("bad") == 0 and row.get("bytes") == args.bytes
                 for row in results)
    valid &= [row.get("sample") for row in results] == list(range(args.samples))
    errors = next((row for row in record["rows"] if row["record"] == "ERRORS"), {})
    if args.mode == "reliable":
        valid &= "LIFECYCLE stream_eof_and_stop=PASS" in lines
    else:
        valid &= errors.get("observed") == 1
        valid &= errors.get("uart1") == 0 and errors.get("uart2") == 0
    if args.fault_us != 0:
        fault = next((row for row in record["rows"] if row["record"] == "FAULT"), {})
        valid &= fault.get("requested_us") == args.fault_us
        valid &= fault.get("fired") == 1 and fault.get("restored") == 1
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
