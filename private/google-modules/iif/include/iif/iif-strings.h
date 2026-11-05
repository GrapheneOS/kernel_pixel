/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IIF string conversion functions.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __IIF_IIF_STRINGS_H__
#define __IIF_IIF_STRINGS_H__

#include <iif/iif-shared.h>

#define IIF_TYPE_SYMBOLS(...) __VA_ARGS__

/* Symbolic mappings of enum values shared between helper functions and trace events header. */
#define IIF_FENCE_SIGNALER_TYPE_SYMBOLS IIF_TYPE_SYMBOLS({ IIF_FENCE_SIGNALER_TYPE_IP, "IP" })

#define IIF_FENCE_TYPE_SYMBOLS                                          \
	IIF_TYPE_SYMBOLS({ IIF_FENCE_TYPE_SINGLE_SHOT, "SINGLE_SHOT" }, \
			 { IIF_FENCE_TYPE_REUSABLE, "REUSABLE" })

#define IIF_IP_TYPE_SYMBOLS                                                                   \
	IIF_TYPE_SYMBOLS({ IIF_IP_DSP, "DSP" }, { IIF_IP_TPU, "TPU" }, { IIF_IP_GPU, "GPU" }, \
			 { IIF_IP_AP, "AP" }, { IIF_IP_IRIS_CORE_0, "IRIS_CORE_0" },          \
			 { IIF_IP_AOC, "AOC" }, { IIF_IP_IRIS_CORE_1, "IRIS_CORE_1" },        \
			 { IIF_IP_IRIS_CORE_2, "IRIS_CORE_2" })

/*
 * iif_signaler_type_to_string() - Converts fence signaler type to string.
 * @signaler_type: The fence signaler type to convert.
 *
 * Return: String representation of the fence signaler type.
 */
const char *iif_signaler_type_to_string(enum iif_fence_signaler_type signaler_type);

/*
 * iif_fence_type_to_string() - Converts fence type to string.
 * @type: The fence type to convert.
 *
 * Return: String representation of the fence type.
 */
const char *iif_fence_type_to_string(enum iif_fence_type type);

/*
 * iif_ip_type_to_string() - Converts IP type to string.
 * @ip_type: The IP type to convert.
 *
 * Return: String representation of the IP type.
 */
const char *iif_ip_type_to_string(enum iif_ip_type ip_type);

#endif /* __IIF_IIF_STRINGS_H__ */
