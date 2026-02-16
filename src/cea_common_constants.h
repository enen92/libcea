/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_CONSTANTS_H
#define CEA_CONSTANTS_H

#include <stdio.h>

#ifndef __cplusplus
#define false 0
#define true 1
#endif

enum cea_debug_message_types
{
	CEA_DMT_TIME = 4,
	CEA_DMT_VERBOSE = 8,
	CEA_DMT_DECODER_608 = 0x10,
	CEA_DMT_708 = 0x20,
	CEA_DMT_CBRAW = 0x80,
	CEA_DMT_GENERIC_NOTICES = 0x100,
};

enum cea_frame_type
{
	CEA_FRAME_TYPE_RESET_OR_UNKNOWN = 0,
	CEA_FRAME_TYPE_I_FRAME = 1,
};

#endif
