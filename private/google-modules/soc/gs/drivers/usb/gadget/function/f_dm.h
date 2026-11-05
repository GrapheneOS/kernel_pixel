/* SPDX-License-Identifier: GPL-2.0-only */

int dm_bind_config(struct usb_configuration *c, u8 port_num);
struct usb_function_instance *alloc_inst_dm(bool dm_config);
struct usb_function *function_alloc_dm(struct usb_function_instance *fi, bool dm_config);
int dm1_bind_config(struct usb_configuration *c, u8 port_num);
struct usb_function_instance *alloc_inst_dm1(bool dm1_config);
struct usb_function *function_alloc_dm1(struct usb_function_instance *fi, bool dm1_config);
