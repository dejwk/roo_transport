load("@roo_testing//roo_testing/emulation:esp_idf.bzl", "roo_esp_idf_example")

roo_esp_idf_example(
    name = "Benchmark",
    srcs = ["Benchmark.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//:roo_transport",
        "@roo_io",
        "@roo_io//:esp32_uart",
        "@roo_logging",
        "@roo_threads",
        "@roo_testing//roo_testing/buses/uart",
        "@roo_testing//roo_testing/frameworks/esp-idf",
        "@roo_testing//roo_testing/microcontrollers/esp32",
    ],
)
