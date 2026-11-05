# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC

"""Malibu platform definitions for CPM reset tests."""

load("//private/google-modules/soc/rdo/common/tests/power:bste_platform_defs.bzl", "bste_platform")

_RESET_DOMAINS = [
    "codec_3p",
    "aoss_pg_amb",
    "lsio_s",
    "lsio_e",
    "hsios",
    # "hsion",  # TODO: b/533035358 - Enable when asserting doesn't disconnect USB.
    "pcie",
]

MBU_RST_DEFS = bste_platform.create(
    target = "//private/devices/google/common:soc_is_malibu",
    binary_srcs = [
        "platform/mbu/rst_defs.c",
    ],
    suite_srcs = [
        "platform/mbu/mbu_rst_tests.sh",
    ],
    test_cases = [
        "test_rst_" + d
        for d in _RESET_DOMAINS
    ],
)
