load("@rules_cc//cc:cc_library.bzl", "cc_library")
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

# The integration test compiles the same sketch rather than duplicating it.
cc_library(
    name = "sketch_under_test",
    testonly = True,
    textual_hdrs = ["Benchmark.ino"],
    visibility = ["//:__pkg__"],
    deps = [
        "//:roo_transport",
        "@roo_testing//roo_testing/buses/uart",
        "@roo_testing//roo_testing/microcontrollers/esp32",
    ],
)
