#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Helper script to run KUnit tests for Google BMS module.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

KUNIT_BUILD_DIR="${ROOT_DIR}/common/ack/.kunit"
KUNIT_PY="${ROOT_DIR}/common/ack/tools/testing/kunit/kunit.py"
MODULE_REL_PATH="../../private/google-modules/bms/test"

if [ ! -f "${KUNIT_PY}" ]; then
    echo "Error: kunit.py not found at ${KUNIT_PY}" >&2
    exit 1
fi

VERBOSE=0
FILTER=""
EXTRA_ARGS=()

RECONFIG=0

show_help() {
    echo "Usage: $0 [-v|--verbose] [-c|--reconfig] [FILTER] [KUNIT_ARGS...]"
    echo ""
    echo "Helper script to run KUnit tests for Google BMS module."
    echo ""
    echo "Options:"
    echo "  -v, --verbose    Enable verbose output, including pr_debug logs."
    echo "  -c, --reconfig   Force re-running Kconfig configuration phase."
    echo "  -h, --help       Show this help message."
    echo ""
    echo "Positional Arguments:"
    echo "  FILTER           KUnit test filter (default: google_ttf_test)"
    echo "  KUNIT_ARGS       Additional arguments forwarded to kunit.py"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -c|--reconfig)
            RECONFIG=1
            shift
            ;;
        -h|--help)
            show_help
            ;;
        *)
            if [ -z "${FILTER}" ]; then
                FILTER="$1"
            else
                EXTRA_ARGS+=("$1")
            fi
            shift
            ;;
    esac
done

KUNIT_FLAGS=()
if [ "${VERBOSE}" -eq 1 ]; then
    KUNIT_FLAGS+=(
        "--raw_output=all"
        "--kernel_args" "ignore_loglevel"
        "--kernel_args" "dyndbg=+p"
    )
fi

if [ ! -f "${KUNIT_BUILD_DIR}/.config" ] || [ "${RECONFIG}" -eq 1 ]; then
    echo "Configuring KUnit kernel..."
    "${KUNIT_PY}" config \
        --kunitconfig "${MODULE_REL_PATH}/kunitconfig" \
        --make_options KCONFIG_EXT_PREFIX="${MODULE_REL_PATH}/" \
        --make_options drivers-y="${MODULE_REL_PATH}/"
fi

"${KUNIT_PY}" build \
    --kunitconfig "${MODULE_REL_PATH}/kunitconfig" \
    --make_options KCONFIG_EXT_PREFIX="${MODULE_REL_PATH}/" \
    --make_options drivers-y="${MODULE_REL_PATH}/"

"${KUNIT_PY}" exec \
    "${KUNIT_FLAGS[@]}" \
    "${FILTER}" "${EXTRA_ARGS[@]}"

