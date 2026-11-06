#!/usr/bin/env python3
"""Run paired finite ESP32 firmware and validate both endpoints' completion."""
import argparse
from contextlib import ExitStack
import json
import math
from pathlib import Path
import re
import statistics
import time

RECORDS = ("READY", "SYSTEM", "CONFIG", "LATENCY", "RESULT", "LIFECYCLE", "DONE")
FATAL = ("Check failed:", "abort()", "Guru Meditation", "ERROR ")


def fields(line):
    values = {}
    for key, value in re.findall(r"(\w+)=([^ ]+)", line):
        try:
            values[key] = int(value)
        except ValueError:
            try:
                values[key] = float(value)
            except ValueError:
                values[key] = value
    return values


def public_manifest(manifest, role):
    """Keep source/build provenance without the builder's absolute paths."""
    clean = dict(manifest)
    clean["sketch"] = "generated/" + role + ".cpp"
    platform = clean.get("platform", "")
    if Path(platform).is_absolute():
        clean["platform"] = Path(platform).name
    return clean


def validate(lines, config):
    """Return rows and failures; summaries are created only for accepted runs."""
    rows = []
    failures = []
    for role in ("server", "client"):
        role_rows = []
        started = False
        for line in lines[role]:
            if line.startswith("CONFIG "):
                started = True
            if any(token in line for token in FATAL) or (started and "rst:0x" in line):
                failures.append(role + ": reset or fatal output")
            if line.split(" ", 1)[0] in RECORDS:
                row = {"record": line.split(" ", 1)[0], "endpoint": role, **fields(line)}
                role_rows.append(row)
                if row.get("role") != role:
                    failures.append(role + ": firmware role mismatch")
        rows.extend(role_rows)
        for record in ("READY", "SYSTEM", "CONFIG", "LIFECYCLE", "DONE"):
            if sum(row["record"] == record for row in role_rows) != 1:
                failures.append(role + ": expected exactly one " + record)
        cfg = next((r for r in role_rows if r["record"] == "CONFIG"), {})
        expected = {"baud": config["baud"], "actual": config["baud"],
                    "tx": 4, "rx": 2, "window": config["window_log"],
                    "bytes": config["bytes"], "runs": config["runs"],
                    "latency_samples": config["samples"], "chunk": config["chunk"],
                    "app_prio": config["io_priority"],
                    "sender_prio": config["io_priority"], "rxbuf": 256,
                    "txbuf": 1024, "rx_fifo": 64, "rx_timeout": 1}
        if any(cfg.get(key) != value for key, value in expected.items()):
            failures.append(role + ": settings do not match the paired build")
        system = next((r for r in role_rows if r["record"] == "SYSTEM"), {})
        if not (str(system.get("chip", "")).startswith("ESP32") and
                system.get("cpu") == 240000000 and system.get("apb") == 80000000):
            failures.append(role + ": missing or unexpected system configuration")
        lifecycle = next((r for r in role_rows if r["record"] == "LIFECYCLE"), {})
        if lifecycle.get("stream_eof_and_stop") != "PASS":
            failures.append(role + ": EOF/stop check did not pass")
        results = [r for r in role_rows if r["record"] == "RESULT"]
        latency = [r for r in role_rows if r["record"] == "LATENCY"]
        if role == "server":
            if results or latency:
                failures.append("server: unexpected client measurements")
            continue
        sequence = [r["record"] for r in role_rows if r["record"] in ("LATENCY", "RESULT")]
        if sequence != ["LATENCY", "RESULT"] * config["runs"]:
            failures.append("client: incomplete or out-of-order measurement cycles")
        if [r.get("sample") for r in results] != list(range(config["runs"])):
            failures.append("client: incorrect result indices")
        for row in results:
            elapsed = row.get("us")
            rate = row.get("mbps")
            if (row.get("bytes") != config["bytes"] or row.get("bad") != 0 or
                    type(elapsed) is not int or elapsed <= 0 or
                    not isinstance(rate, (int, float)) or not math.isfinite(rate) or
                    abs(rate - config["bytes"] * 8 / elapsed) > 0.000001):
                failures.append("client: invalid payload, elapsed time or rate")
        for row in latency:
            quantiles = [row.get(key) for key in ("min_ms", "p50_ms", "p90_ms", "p99_ms", "max_ms")]
            if (not all(isinstance(v, (int, float)) and math.isfinite(v) and v >= 0
                        for v in quantiles) or quantiles != sorted(quantiles)):
                failures.append("client: invalid latency summary")
    return rows, failures


def summaries(rows):
    results = [row for row in rows if row["record"] == "RESULT"]
    rates = [row["bytes"] * 8 / row["us"] for row in results]
    latency = [row for row in rows if row["record"] == "LATENCY"]
    return {
        "payload_mbit_s": {"min": min(rates), "mean": statistics.mean(rates),
                           "max": max(rates), "aggregate": sum(r["bytes"] for r in results) * 8 /
                           sum(r["us"] for r in results)},
        "latency_ms": {"batches": len(latency),
                       "samples_per_batch": None,
                       "p50_min": min(r["p50_ms"] for r in latency),
                       "p50_max": max(r["p50_ms"] for r in latency),
                       "p99_min": min(r["p99_ms"] for r in latency),
                       "p99_max": max(r["p99_ms"] for r in latency)},
    }


def capture(ports, reset, timeout):
    """Open both consoles before reset, then start only after both are ready."""
    import serial

    lines = {role: [] for role in ports}
    buffers = {role: b"" for role in ports}
    failures = []
    started = False
    with ExitStack() as stack:
        opened = {}
        for role, device in ports.items():
            port = serial.Serial(timeout=0)
            port.port = device
            port.baudrate = 115200
            port.dtr = False
            port.rts = False
            port.open()
            opened[role] = stack.enter_context(port)
        if reset:
            for port in opened.values():
                port.rts = True
            time.sleep(0.1)
            for port in opened.values():
                port.reset_input_buffer()
            for port in opened.values():
                port.rts = False
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            fatal = False
            for role, port in opened.items():
                count = port.in_waiting
                if count == 0:
                    continue
                buffers[role] += port.read(count)
                while b"\n" in buffers[role]:
                    raw, buffers[role] = buffers[role].split(b"\n", 1)
                    line = raw.decode(errors="replace").strip()
                    if not line:
                        continue
                    lines[role].append(line)
                    print(role + ": " + line, flush=True)
                    fatal |= any(token in line for token in FATAL)
                    fatal |= started and "rst:0x" in line
            if fatal:
                failures.append("Firmware reset or fatal output")
                break
            if not started and all("READY role=" + role in lines[role] for role in ports):
                for port in opened.values():
                    port.write(b"run\n")
                    port.flush()
                started = True
            if all("DONE role=" + role in lines[role] for role in ports):
                break
            time.sleep(0.005)
        else:
            failures.append("Timed out waiting for both finite endpoints")
    return lines, failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-port", required=True)
    parser.add_argument("--client-port", required=True)
    parser.add_argument("--firmware", type=Path, required=True,
                        help="Output root from build_two_board.py")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if args.server_port == args.client_port or args.timeout <= 0:
        parser.error("Use two distinct ports and a positive timeout")
    pair = json.loads((args.firmware / "pair-manifest.json").read_text())
    if set(pair["firmware"]) != {"server", "client"}:
        parser.error("Paired firmware must be built, not only generated")
    ports = {"server": args.server_port, "client": args.client_port}
    lines = {role: [] for role in ports}
    failures = []
    try:
        lines, failures = capture(ports, args.reset, args.timeout)
    except OSError:
        failures.append("Serial acquisition failed; check the endpoint consoles")
    rows, validation = validate(lines, pair["configuration"])
    failures.extend(validation)
    record = {"schema_version": 1, "topology": "two-board-uart",
              "parameters": pair["configuration"], "rows": rows,
              "passed": not failures, "failures": failures,
              "example_sha256": pair["example_sha256"],
              "firmware": {role: public_manifest(value, role)
                           for role, value in pair["firmware"].items()}}
    if record["passed"]:
        record.update(summaries(rows))
        record["latency_ms"]["samples_per_batch"] = pair["configuration"]["samples"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for role, text in lines.items():
        args.output.with_suffix("." + role + ".log").write_text("\n".join(text) + "\n")
    args.output.write_text(json.dumps(record, indent=2) + "\n")
    if failures:
        raise SystemExit("Two-board benchmark rejected: " + "; ".join(failures))
    print("PASS payload Mbit/s:", json.dumps(record["payload_mbit_s"]), flush=True)
    print("Latency is reported as per-batch median/p99 ranges, not pooled quantiles.")


if __name__ == "__main__":
    main()
