# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC

"""Malibu platform definitions for power controller power domain tests."""

load("//private/google-modules/soc/rdo/common/tests/power:bste_platform_defs.bzl", "bste_platform")

_POWER_DOMAINS = [
    "aurdsp",
    "aoss_pg",
    "codec_3p",
    "cpuacc",
    "g2d",
    "gcv",
    "gpu",
    "tpu",
    "memss_dta",
    "dpu",
    # "hsio_n",  # TODO: b/526571556 - Enable when we define a way to test this.
    "hsio_s",
    "ispfe",
    "ispbe",
    "lsio_e",
    "lsio_s",
    "pcie",
]

MBU_PD_DEFS = bste_platform.create(
    target = "//private/devices/google/common:soc_is_malibu",
    binary_srcs = [
        "power-domain/platform/mbu/pd_defs.c",
    ],
    suite_srcs = [
        "power-domain/platform/mbu/mbu_tests.sh",
    ],
    test_cases = [
        "test_domain_" + d
        for d in _POWER_DOMAINS
    ],
)
