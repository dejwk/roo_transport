#!/usr/bin/env python3
"""Build the Pico hardware benchmark from sibling roo source checkouts."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

LIBRARIES = (
    "roo_transport", "roo_io", "roo_collections", "roo_threads", "roo_logging",
    "roo_time", "roo_flags", "roo_backport",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True,
                        help="Arduino-Pico-capable PlatformIO platform or installed path")
    parser.add_argument("--sketch", type=Path,
                        help="Build an example sketch instead of the measurement harness")
    parser.add_argument("--board", default="waveshare_rp2040_zero")
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path,
                        default=Path.home() / ".cache/roo_transport/pico-benchmark/firmware")
    parser.add_argument("--test-rx-count", type=int,
                        help="Force frequent RX DMA counter renewals (e.g. 65536)")
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
                "platform": args.platform, "test_rx_count": args.test_rx_count}
    for name in LIBRARIES:
        checkout = repositories / name
        destination = output / "local-libs" / name
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(checkout / "src", destination / "src")
        library = json.loads((checkout / "library.json").read_text())
        library["dependencies"] = {}  # All dependencies come from local snapshots.
        (destination / "library.json").write_text(json.dumps(library, indent=2) + "\n")
        manifest["repositories"][name] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
    shutil.copy2(sketch, output / "src/main.cpp")
    if args.sketch is None:
        shutil.copy2(source / "raw_dma_stream.h", output / "src/raw_dma_stream.h")
    else:
        (output / "src/raw_dma_stream.h").unlink(missing_ok=True)
    manifest["sketch"] = str(sketch)
    flags = "-fno-exceptions -DPIO_FRAMEWORK_ARDUINO_ENABLE_FREERTOS"
    if args.test_rx_count is not None:
        if args.test_rx_count < 16384:
            parser.error("--test-rx-count must be at least 16384")
        flags += f" -DROO_TRANSPORT_PICO_DMA_TEST_TRANSFER_COUNT={args.test_rx_count}"
    flags += " " + " ".join(args.extra_flag)
    manifest["build_flags"] = flags.split()
    (output / "platformio.ini").write_text(
        "[platformio]\nlib_dir = local-libs\nlibdeps_dir = local-dependencies\n"
        f"[env:benchmark]\nplatform = {args.platform}\nboard = {args.board}\n"
        "framework = arduino\nboard_build.core = earlephilhower\n"
        f"lib_ldf_mode = deep+\nbuild_flags = {flags}\n")
    subprocess.run([args.pio, "run", "-j", str(args.jobs)], cwd=output, check=True)
    for directory in (output / "src", output / "local-libs"):
        for file in sorted(directory.rglob("*")):
            if file.is_file():
                manifest["sources"][str(file.relative_to(output))] = hashlib.sha256(
                    file.read_bytes()).hexdigest()
    firmware = output / ".pio/build/benchmark/firmware.uf2"
    manifest["firmware_sha256"] = hashlib.sha256(firmware.read_bytes()).hexdigest()
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Firmware: {firmware}\nManifest: {output / 'manifest.json'}")


if __name__ == "__main__":
    main()
