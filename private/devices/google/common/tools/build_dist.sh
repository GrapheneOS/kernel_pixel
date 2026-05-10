#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/envsetup.sh"

DEVICE="$1"
shift

if [[ -z "${DEVICE}" ]]; then
  cat >&2 <<EOF
usage: $0 <device> [<options>]

Build the distribution package of a device.

EOF
  exit 1
fi

"${WORKSPACE_DIR}/tools/bazel" run \
  --config=stamp \
  --config="${DEVICE}" \
  "//private/devices/google/${DEVICE}:${DEVICE}/dist" \
  "$@" || exit 1

sign_file=$(mktemp)
trap '{ rm -f -- "$sign_file"; }' EXIT
prebuilts/clang/host/linux-x86/clang-r510928/bin/clang aosp/scripts/sign-file.c -lssl -lcrypto -o ${sign_file}
find out/${DEVICE}/dist -type f -name "*.ko" \
  -exec ${sign_file} sha256 \
  $(find out/ -type f -name "signing_key.pem") \
  $(find out/ -type f -name "signing_key.x509") {} \;