/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP assertion macros.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __GCIP_ASSERT_H__
#define __GCIP_ASSERT_H__

#include <gcip/gcip-config.h>
#include <linux/compiler.h>

/**
 * GCIP_DEBUG_ASSERT() - Assert that a condition is true.
 * @cond: The condition to assert.
 *
 * Do not pass expressions with function calls in @cond.
 * The assertions are expected to be eliminated in production, using function in the expression may
 * prevent the compiler from optimizing out the assertion code.
 *
 * Return: True if the assertion failed (condition was false), false otherwise.
 */
#define GCIP_DEBUG_ASSERT(cond)       \
	({                            \
		bool __ret = !(cond); \
		unlikely(__ret);      \
	})

#endif /* __GCIP_ASSERT_H__ */
