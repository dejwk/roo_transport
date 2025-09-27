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
