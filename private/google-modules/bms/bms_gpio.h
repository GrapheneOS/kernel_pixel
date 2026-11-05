/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __BMS_GPIO_H
#define __BMS_GPIO_H

#include <dt-bindings/gpio/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

/*
 * These shared Pixel charging GPIOs use the standard two-cell binding.
 * Read their polarity without requesting a line owned by another consumer.
 */
static inline int bms_of_get_gpio(struct device_node *node, const char *name,
				int *active_low)
{
	struct of_phandle_args args;
	int gpio, ret;

	ret = of_parse_phandle_with_args(node, name, "#gpio-cells", 0, &args);
	if (ret)
		return ret;

	of_node_put(args.np);
	if (args.args_count != 2)
		return -EINVAL;

	gpio = of_get_named_gpio(node, name, 0);
	if (gpio >= 0)
		*active_low = !!(args.args[1] & GPIO_ACTIVE_LOW);
	return gpio;
}

#endif /* __BMS_GPIO_H */
