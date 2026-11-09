#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

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
  --extra_git_project=private \
  --config="${DEVICE}" \
  "//private/devices/google/${DEVICE}:${DEVICE}/dist" \
  "$@" || exit 1

# Sign the flat modules imported by the OS build with this GKI's key.
# Raw Kleaf images and module archives are not modified here.
gki_out="${WORKSPACE_DIR}/bazel-bin/common/kernel_aarch64"
dist="${WORKSPACE_DIR}/out/${DEVICE}/dist"
cmp "${dist}/Image" "${gki_out}/Image"
for module in "${dist}"/*.ko; do
  "${gki_out}/scripts/sign-file" sha256 \
    "${gki_out}/certs/signing_key.pem" \
    "${gki_out}/certs/signing_key.x509" "${module}"
done
