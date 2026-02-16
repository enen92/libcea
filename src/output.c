/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_decoders_structs.h"
#include "cea_common_common.h"

void printdata(struct lib_cc_decode *ctx, const unsigned char *data1, int length1,
	       const unsigned char *data2, int length2, struct cc_subtitle *sub)
{
	if (length1 && ctx->extract != 2)
	{
		ctx->current_field = 1;
		ctx->writedata(data1, length1, ctx, sub);
	}
	if (length2 && ctx->extract != 1)
	{
		ctx->current_field = 2;
		ctx->writedata(data2, length2, ctx, sub);
	}
}
