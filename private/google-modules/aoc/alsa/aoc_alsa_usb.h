/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AOC_ALSA_USB_H
#define AOC_ALSA_USB_H

#include <linux/types.h>

struct usb_device;
struct usb_host_endpoint;

/*
 * Callbacks may sleep and must not reenter this registration API or USB sound
 * platform registration. The caller must finish initialization before publish,
 * keep ops alive until unregister returns, and drain its work after unregister.
 */
struct aoc_alsa_usb_ops {
	void (*setup_transfer_ring)(struct usb_device *udev,
				    struct usb_host_endpoint *ep);
	void (*connection)(bool connected);
};

int aoc_alsa_usb_callback_register(const struct aoc_alsa_usb_ops *ops);
void aoc_alsa_usb_callback_unregister(const struct aoc_alsa_usb_ops *ops);

#endif
