// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIF string conversion functions.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <iif/iif-shared.h>
#include <iif/iif-strings.h>

struct iif_symbol_name {
	int val;
	const char *name;
};

static const struct iif_symbol_name signaler_type_symbols[] = { IIF_FENCE_SIGNALER_TYPE_SYMBOLS };
static const struct iif_symbol_name fence_type_symbols[] = { IIF_FENCE_TYPE_SYMBOLS };
static const struct iif_symbol_name ip_type_symbols[] = { IIF_IP_TYPE_SYMBOLS };

static const char *iif_lookup_symbol_name(const struct iif_symbol_name *symbols, size_t num_symbols,
					  int val)
{
	size_t i;

	for (i = 0; i < num_symbols; i++) {
		if (symbols[i].val == val)
			return symbols[i].name;
	}

	return "UNKNOWN";
}

const char *iif_signaler_type_to_string(enum iif_fence_signaler_type signaler_type)
{
	return iif_lookup_symbol_name(signaler_type_symbols, ARRAY_SIZE(signaler_type_symbols),
				      signaler_type);
}

const char *iif_fence_type_to_string(enum iif_fence_type type)
{
	return iif_lookup_symbol_name(fence_type_symbols, ARRAY_SIZE(fence_type_symbols), type);
}

const char *iif_ip_type_to_string(enum iif_ip_type type)
{
	return iif_lookup_symbol_name(ip_type_symbols, ARRAY_SIZE(ip_type_symbols), type);
}
