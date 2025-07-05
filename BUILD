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
        "//lib/roo_io",
        "//lib/roo_io_arduino",
        "//lib/roo_logging",
        "//lib/roo_threads",
    ],
)

cc_library(
    name = "testing",
    srcs = glob(
        [
            "src/**/*.cpp",
            "src/**/*.h",
        ],
        exclude = ["test/**"],
    ),
    defines = ["ROO_IO_TESTING"],
    alwayslink = 1,
    includes = [
        "src",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":roo_io",
        "//roo_testing:arduino_gtest_main",
    ],
)
