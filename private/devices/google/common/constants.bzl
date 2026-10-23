# SPDX-License-Identifier: GPL-2.0-only

"""
Common Pixel constants.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("//common:bazel/modules.bzl", "get_gki_modules_list")

# GKI modules replaced by Pixel modules or omitted from Pixel images.
_OMITTED_GKI_MODULES = [
    # keep sorted
    "arch/arm64/geniezone/gzvm.ko",
    "drivers/block/zram/zram.ko",
    "drivers/ptp/ptp_kvm.ko",
    "net/tls/tls.ko",
]

def _pixel_gki_basename(module):
    return None if module in _OMITTED_GKI_MODULES else paths.basename(module)

def _pixel_gki_path(module):
    return None if module in _OMITTED_GKI_MODULES else "^kernel/" + module

PIXEL_GKI_MODULES_BASENAMES = get_gki_modules_list("arm64", map_each = _pixel_gki_basename)
PIXEL_GKI_MODULES_PATHS = get_gki_modules_list("arm64", map_each = _pixel_gki_path)
