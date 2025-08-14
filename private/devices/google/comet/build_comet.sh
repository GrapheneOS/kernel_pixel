#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

exec tools/build_dist.sh comet --disable_slab_canary "$@"
