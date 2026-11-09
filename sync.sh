#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

cd "$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
repo init --standalone-manifest -u "file://${PWD}/aosp_manifest.xml" \
  --groups=dependencies --partial-clone
repo sync --current-branch --no-tags "$@"
