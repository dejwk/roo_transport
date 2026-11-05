#!/usr/bin/env python3
"""Build the ESP32 UART hardware benchmark from sibling roo source checkouts."""
import argparse
import hashlib
import json
import io
import tarfile
import shutil
import subprocess
from pathlib import Path

LIBRARIES = (
    "roo_transport", "roo_io", "roo_collections", "roo_threads", "roo_logging",
    "roo_time", "roo_flags", "roo_backport",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", default="espressif32",
                        help="Arduino-ESP32-capable PlatformIO platform or installed path")
    parser.add_argument("--sketch", type=Path,
                        help="Build an example sketch instead of the measurement harness")
    parser.add_argument("--board", default="esp32dev")
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path,
                        default=Path.home() / ".cache/roo_transport/esp32-benchmark/firmware")
    parser.add_argument("--transport-ref", help="Build roo_transport src from a Git commit for controlled comparisons")
    parser.add_argument("--cpu-mhz", type=int, default=240)
    parser.add_argument("--uart-event-core", type=int, choices=(-1, 0, 1))
    parser.add_argument("--uart-event-priority", type=int)
    parser.add_argument("--extra-flag", action="append", default=[])
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    sketch = args.sketch.expanduser().resolve() if args.sketch else source / "main.cpp"
    if not sketch.is_file():
        parser.error(f"Sketch does not exist: {sketch}")
    repositories = source.parents[1].parent
    output = args.output.expanduser().resolve()
    (output / "src").mkdir(parents=True, exist_ok=True)
    manifest = {"repositories": {}, "sources": {}, "board": args.board,
                "platform": args.platform, "cpu_mhz": args.cpu_mhz,
                "uart_event_core": args.uart_event_core,
                "uart_event_priority": args.uart_event_priority,
                "transport_ref": args.transport_ref or "worktree"}
    for name in LIBRARIES:
        checkout = repositories / name
        destination = output / "local-libs" / name
        if destination.exists():
            shutil.rmtree(destination)
        if name == "roo_transport" and args.transport_ref is not None:
            revision = subprocess.check_output(
                ["git", "rev-parse", "--verify", args.transport_ref + "^{commit}"],
                cwd=checkout, text=True).strip()
            archive = subprocess.check_output(
                ["git", "archive", "--format=tar", revision, "src"], cwd=checkout)
            destination.mkdir(parents=True)
            with tarfile.open(fileobj=io.BytesIO(archive)) as sources:
                sources.extractall(destination, filter="data")
        else:
            revision = "HEAD"
            shutil.copytree(checkout / "src", destination / "src")
        library = json.loads((checkout / "library.json").read_text())
        library["dependencies"] = {}  # All dependencies come from local snapshots.
        (destination / "library.json").write_text(json.dumps(library, indent=2) + "\n")
        manifest["repositories"][name] = subprocess.check_output(
            ["git", "rev-parse", revision], cwd=checkout, text=True).strip()
    shutil.copy2(sketch, output / "src/main.cpp")
    manifest["sketch"] = str(sketch)
    flags = ["-fno-exceptions", "-DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=4096"]
    if args.uart_event_core is not None:
        flags.append(f"-DARDUINO_SERIAL_EVENT_TASK_RUNNING_CORE={args.uart_event_core}")
    if args.uart_event_priority is not None:
        flags.append(f"-DARDUINO_SERIAL_EVENT_TASK_PRIORITY={args.uart_event_priority}")
    flags.extend(args.extra_flag)
    manifest["build_flags"] = flags
    (output / "platformio.ini").write_text(
        "[platformio]\nlib_dir = local-libs\nlibdeps_dir = local-dependencies\n"
        f"[env:benchmark]\nplatform = {args.platform}\nboard = {args.board}\n"
        f"framework = arduino\nboard_build.f_cpu = {args.cpu_mhz * 1000000}L\n"
        "upload_speed = 115200\nlib_ldf_mode = deep\n"
        f"build_flags = {' '.join(flags)}\n")
    subprocess.run([args.pio, "run", "-j", str(args.jobs)], cwd=output, check=True)
    for directory in (output / "src", output / "local-libs"):
        for file in sorted(directory.rglob("*")):
            if file.is_file():
                manifest["sources"][str(file.relative_to(output))] = hashlib.sha256(
                    file.read_bytes()).hexdigest()
    firmware = output / ".pio/build/benchmark/firmware.bin"
    manifest["firmware_sha256"] = hashlib.sha256(firmware.read_bytes()).hexdigest()
    manifest["artifacts_sha256"] = {
        file.name: hashlib.sha256(file.read_bytes()).hexdigest()
        for file in firmware.parent.glob("*.bin")
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Firmware: {firmware}\nManifest: {output / 'manifest.json'}")


if __name__ == "__main__":
    main()
