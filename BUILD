cc_library(
    name = "roo_transport",
    srcs = glob(
        [
            "src/**/*.cpp",
            "src/**/*.h",
        ],
        exclude = ["test/**"],
    ),
    includes = [
        "src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@roo_collections",
        "@roo_io",
        "@roo_io_arduino",
        "@roo_logging",
        "@roo_threads",
    ],
)

cc_test(
    name = "packets_over_stream_test",
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
    size = "small",
)

cc_test(
    name = "link_transport_test",
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
    size = "small",
)