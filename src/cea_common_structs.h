/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef _CEA_COMMON_STRUCTS
#define _CEA_COMMON_STRUCTS

#include "cea_common_constants.h"

enum subtype
{
	CC_608,
	CC_TEXT,
};

struct cc_subtitle
{
	void *data;
	unsigned int nb_data;
	enum subtype type;

	int64_t start_time;
	int64_t end_time;

	int flags;
	int got_output;

	char mode[5];
	char info[4];

	struct cc_subtitle *next;
};
#endif
