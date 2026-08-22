load("@roo_testing//roo_testing/emulation:arduino.bzl", "roo_arduino_example")

roo_arduino_example(
    name = "Benchmark",
    sketch = "Benchmark.ino",
    visibility = ["//visibility:public"],
    deps = [
        "//:roo_transport",
        "@roo_testing//roo_testing/buses/uart",
        "@roo_testing//roo_testing/microcontrollers/esp32",
    ],
)
