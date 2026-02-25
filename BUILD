load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "roo_transport",
    srcs = glob(
        [
            "src/**/*.cpp",
            "src/**/*.h",
        ],
        exclude = ["test/**"],
    ),
    defines = [
        "MLOG_roo_transport_reliable_channel_connection=1",
    ],
    includes = [
        "src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@roo_collections",
        "@roo_io",
        "@roo_logging",
        "@roo_threads",
    ],
)

cc_test(
    name = "packets_over_stream_test",
    size = "small",
    srcs = [
        "test/packets_over_stream_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    includes = ["src"],
    linkstatic = 1,
    deps = [
        ":roo_transport",
        "//test/helpers",
        "@roo_testing//:arduino_gtest_main",
    ],
)

cc_test(
    name = "link_transport_test",
    size = "small",
    srcs = [
        "test/link_transport_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    includes = ["src"],
    linkstatic = 1,
    deps = [
        ":roo_transport",
        "//test/helpers",
        "@roo_testing//:arduino_gtest_main",
    ],
)

cc_test(
    name = "serial_link_transport_test",
    size = "small",
    srcs = [
        "test/serial_link_transport_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    includes = ["src"],
    linkstatic = 1,
    deps = [
        ":roo_transport",
        "//test/helpers",
        "@roo_testing//:arduino_gtest_main",
        "@roo_testing//roo_testing/microcontrollers/esp32",
    ],
)

cc_test(
    name = "link_messaging_test",
    size = "small",
    srcs = [
        "test/link_messaging_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    includes = ["src"],
    linkstatic = 1,
    deps = [
        ":roo_transport",
        "//test/helpers",
        "@roo_io",
        "@roo_testing//:arduino_gtest_main",
    ],
)
