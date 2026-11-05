# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC

"""Malibu platform definitions for CPM reset controller tests."""

MBU_RST_CC_BINARY_SRCS = [
    "cpm-reset/platform/mbu/rst_defs.c",
]

MBU_RST_TEST_SUITE_SRCS = [
    "cpm-reset/platform/mbu/mbu_rst_tests.sh",
]

MBU_RST_TEST_CASES = [
    "test_rst_codec_3p",
    "test_rst_aoss_pg_amb",
    "test_rst_lsio_s",
    "test_rst_lsio_e",
    # "test_rst_hsion",  # TODO: b/533035358 - Enable when asserting doesn't disconnect USB.
    "test_rst_hsios",
    "test_rst_pcie",
]
