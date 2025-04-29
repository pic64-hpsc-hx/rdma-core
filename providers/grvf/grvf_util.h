// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2023-2024, Grovf, Inc. All rights reserved.
 */

#ifndef GRVF_UTIL_H
#define GRVF_UTIL_H

#include <stdint.h>

static inline uint32_t upper_power_of_two(uint32_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;

	return v;
}

#endif
