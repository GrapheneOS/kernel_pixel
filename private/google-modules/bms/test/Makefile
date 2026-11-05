# SPDX-License-Identifier: GPL-2.0-or-later

ccflags-y += -I$(srctree)/../../private/google-modules/bms/misc/include \
             -Wno-unknown-pragmas -Wno-format -Wno-unused-variable \
             -Wno-frame-larger-than -DDEBUG -Werror=missing-prototypes

obj-$(CONFIG_GOOGLE_TTF_TEST) += google_ttf_test.o
obj-$(CONFIG_GOOGLE_BATTERY_TTF_TESTS) += google_battery_ttf_tests.o
CFLAGS_google_battery_ttf_tests.o += -Wno-enum-conversion -Wno-switch -Wno-incompatible-pointer-types
