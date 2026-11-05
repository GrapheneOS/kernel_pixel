# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC

"""Malibu platform definitions for CPM clock tests."""

load("//private/google-modules/soc/rdo/common/tests/power:bste_platform_defs.bzl", "bste_platform")

_CLOCK_GROUPS = [
    "aoss_pg",
    "cpu",
    "cpuacc",
    "dpu",
    "gpcm_amb",
    "gpcm_infra",
    "hsio_n",
    "hsio_s",
    "lsio_e",
    "lsio_s",
    "pcie",
]

MBU_CLK_DEFS = bste_platform.create(
    target = "//private/devices/google/common:soc_is_malibu",
    binary_srcs = [
        "platform/mbu/clk_defs.c",
    ],
    suite_srcs = [
        "platform/mbu/mbu_clk_tests.sh",
    ],
    test_cases = [
        "test_clk_state_" + g
        for g in _CLOCK_GROUPS
    ] + [
        "test_clk_rate_" + g
        for g in _CLOCK_GROUPS
    ],
)
