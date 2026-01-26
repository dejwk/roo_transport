load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "helpers",
    srcs = glob(
        [
            "**/*.cpp",
            "**/*.h",
        ],
        exclude = ["test/**"],
    ),
    visibility = ["//visibility:public"],
    deps = [
        "//:roo_transport",
    ],
)
