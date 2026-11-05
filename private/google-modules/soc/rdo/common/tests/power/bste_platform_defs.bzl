# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC

"""BSTE platform test definition object and selector helpers."""

def _create(target, binary_srcs = [], suite_srcs = [], test_cases = []):
    """Factory for creating platform test definitions with SOC target binding."""
    return struct(
        target = target,
        binary_srcs = binary_srcs,
        suite_srcs = suite_srcs,
        test_cases = test_cases,
    )

def _select(*defs):
    """Aggregates positional platform definitions into a single platform_defs object."""
    binary_map = {"//conditions:default": []}
    suite_map = {"//conditions:default": []}
    test_cases_map = {"//conditions:default": []}

    for d in defs:
        binary_map[d.target] = d.binary_srcs
        suite_map[d.target] = d.suite_srcs
        test_cases_map[d.target] = d.test_cases

    return struct(
        binary_srcs = select(binary_map),
        suite_srcs = select(suite_map),
        test_cases = select(test_cases_map),
    )

bste_platform = struct(
    create = _create,
    select = _select,
)
