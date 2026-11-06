# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for GCIP framework.
#
# Arguments
#   GMODULE_SRC_PATH: The path of directory containing source of google-modules.
#                     (default: $(ROOT_DIR)/private/google-modules)

CONFIG_GCIP		?= m
obj-$(CONFIG_GCIP)	+= gcip.o

include $(src)/objects.mk
gcip-objs := $(gcip-objects)

CURRENT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# TODO(b/336717718): Remove path of embedded IIF
ccflags-y	+= -I$(CURRENT_DIR)/../../include \
		   -I$(CURRENT_DIR)/iif/include \

# Set the path of google-modules sources if not defined.
GMODULE_SRC_PATH ?= $(ROOT_DIR)/private/google-modules

ifneq ($(wildcard $(GMODULE_SRC_PATH)/iif/include),)
ccflags-y	+= -I$(GMODULE_SRC_PATH)/iif/include
endif



modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
