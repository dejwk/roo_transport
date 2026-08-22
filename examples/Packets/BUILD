load("@roo_testing//roo_testing/emulation:arduino.bzl", "roo_arduino_example")

roo_arduino_example(
    name = "Packets",
    sketch = "Packets.ino",
    deps = [
        "//:roo_transport",
        "@roo_io",
        "@roo_io//:arduino_stream",
        "@roo_threads",
        "@roo_testing//roo_testing/buses/uart",
        "@roo_testing//roo_testing/microcontrollers/esp32",
    ],
)
