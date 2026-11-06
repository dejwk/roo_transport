#!/usr/bin/env python3
"""Offline checks for paired source generation and saved-console validation."""
import importlib.util
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


builder = load("build_two_board")
runner = load("run_two_board")
CONFIG = dict(runs=2, bytes=16385, samples=5, baud=5000000, window_log=3,
              chunk=256, io_priority=1)


def console(role):
    lines = ["READY role=" + role,
             "SYSTEM role=" + role + " chip=ESP32-D0WD-V3 revision=301 sdk=test cpu=240000000 apb=80000000",
             "CONFIG role=" + role + " baud=5000000 actual=5000000 tx=4 rx=2 window=3 bytes=16385 runs=2 latency_samples=5 chunk=256 app_prio=1 sender_prio=1 rxbuf=256 txbuf=1024 rx_fifo=64 rx_timeout=1"]
    if role == "client":
        for sample in range(2):
            lines += ["LATENCY role=client min_ms=1 p50_ms=2 p90_ms=3 p99_ms=4 max_ms=5",
                      f"RESULT role=client sample={sample} bytes=16385 us=100000 mbps=1.310800 bad=0"]
    return lines + ["LIFECYCLE role=" + role + " stream_eof_and_stop=PASS",
                    "DONE role=" + role]


class TwoBoardToolsTest(unittest.TestCase):
    # Verifies each generated role uses the requested independent-board wiring
    # while retaining the example's actual protocol and finite cleanup calls.
    def test_source_roles_and_gate(self):
        original = (HERE.parents[1] / "examples/ReliableSerial/Benchmark/Benchmark.ino").read_text()
        for role in ("server", "client"):
            with self.subTest(role=role):
                source = builder.make_source(original, role, CONFIG)
                self.assertIn("static constexpr Mode kMode = Mode::k" + role.capitalize() + ";", source)
                self.assertIn("static constexpr int kPinClientTx = 4;", source)
                self.assertIn("static constexpr int kPinClientRx = 2;", source)
                self.assertIn("static constexpr uint32_t kRuns = 2;", source)
                self.assertIn("uart_get_baudrate(UART_NUM_" + ("1" if role == "server" else "2"), source)
                setup = source[source.index("void setup() {"):]
                self.assertLess(setup.index("READY role="), setup.index("RunBenchmark(kRuns, kPayloadBytes);"))
                self.assertLess(setup.index("RunBenchmark(kRuns, kPayloadBytes);"), setup.index("LIFECYCLE role="))
                self.assertIn("CHECK_EQ(memcmp(received.get(), pattern, count), 0);", source)
                self.assertIn("CHECK_EQ(in.status(), roo_io::kEndOfStream);", source)
                self.assertNotIn("#define", source)

    # Verifies source drift fails explicitly rather than silently adapting a
    # different configuration or duplicating instrumentation.
    def test_source_drift_is_rejected(self):
        with self.assertRaises(ValueError):
            builder.replace_once("missing", "expected", "replacement")
        with self.assertRaises(ValueError):
            builder.replace_once("twice twice", "twice", "replacement")

    # Verifies two complete role transcripts are accepted and byte/time records
    # produce rates without relying on the firmware's rounded printout.
    def test_complete_pair_is_accepted(self):
        rows, failures = runner.validate({role: console(role) for role in ("server", "client")}, CONFIG)
        self.assertEqual(failures, [])
        summary = runner.summaries(rows)
        self.assertAlmostEqual(summary["payload_mbit_s"]["mean"], 1.3108)
        self.assertAlmostEqual(summary["payload_mbit_s"]["aggregate"], 1.3108)
        self.assertEqual(summary["latency_ms"]["batches"], 2)
        self.assertEqual(summary["latency_ms"]["p99_max"], 4)

    # Verifies malformed, incomplete and failed runs are not accepted as useful
    # throughput, including a finished client paired with an unfinished server.
    def test_invalid_pairs_are_rejected(self):
        cases = {
            "server_not_done": ("server", "DONE role=server", ""),
            "client_not_done": ("client", "DONE role=client", ""),
            "bad_payload": ("client", "bad=0", "bad=1"),
            "truncated_payload": ("client", "sample=0 bytes=16385", "sample=0 bytes=16384"),
            "incorrect_rate": ("client", "mbps=1.310800", "mbps=9.000000"),
            "nonfinite_rate": ("client", "mbps=1.310800", "mbps=nan"),
            "wrong_clock": ("server", "cpu=240000000", "cpu=160000000"),
            "zero_time": ("client", "us=100000", "us=0"),
            "fractional_time": ("client", "us=100000", "us=100000.0"),
            "wrong_index": ("client", "sample=1", "sample=0"),
            "wrong_window": ("server", "window=3", "window=4"),
            "wrong_actual_baud": ("client", "actual=5000000", "actual=4000000"),
            "wrong_sender": ("server", "sender_prio=1", "sender_prio=23"),
            "wrong_role": ("server", "role=server", "role=client"),
            "missing_lifecycle": ("server", "stream_eof_and_stop=PASS", "stream_eof_and_stop=FAIL"),
            "invalid_latency": ("client", "p99_ms=4", "p99_ms=99"),
            "panic": ("client", "DONE role=client", "Guru Meditation\nDONE role=client"),
            "reset": ("client", "DONE role=client", "rst:0xc\nDONE role=client"),
        }
        for label, (role, old, new) in cases.items():
            with self.subTest(case=label):
                lines = {name: console(name) for name in ("server", "client")}
                transcript = "\n".join(lines[role])
                self.assertIn(old, transcript)
                lines[role] = transcript.replace(old, new).splitlines()
                unused_rows, failures = runner.validate(lines, CONFIG)
                self.assertTrue(failures)

    # Verifies public evidence retains exact relative source hashes while
    # excluding the build host's absolute sketch and platform locations.
    def test_public_manifest_has_no_build_paths(self):
        original = {"platform": "/home/example/espressif32", "sketch": "/private/client.cpp",
                    "sources": {"src/main.cpp": "source-hash"}}
        result = runner.public_manifest(original, "client")
        self.assertEqual(result["platform"], "espressif32")
        self.assertEqual(result["sketch"], "generated/client.cpp")
        self.assertEqual(result["sources"], original["sources"])
        self.assertEqual(original["sketch"], "/private/client.cpp")


if __name__ == "__main__":
    unittest.main()
