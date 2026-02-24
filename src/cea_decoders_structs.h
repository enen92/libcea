/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_DECODERS_STRUCTS_H
#define CEA_DECODERS_STRUCTS_H

#include "cea_common_constants.h"
#include "cea_common_timing.h"
#include "cea_common_structs.h"
#include "cea_decoders_708.h"

// Define max width in characters/columns on the screen
#define CEA_DECODER_608_SCREEN_ROWS 15
#define CEA_DECODER_608_SCREEN_WIDTH 32

enum cea_eia608_format
{
	SFORMAT_CC_SCREEN,
	SFORMAT_CC_LINE
};

enum cc_modes
{
	MODE_POPON = 0,
	MODE_ROLLUP_2 = 1,
	MODE_ROLLUP_3 = 2,
	MODE_ROLLUP_4 = 3,
	MODE_TEXT = 4,
	MODE_PAINTON = 5,
};

enum font_bits
{
	FONT_REGULAR = 0,
	FONT_ITALICS = 1,
	FONT_UNDERLINED = 2,
	FONT_UNDERLINED_ITALICS = 3
};

enum cea_decoder_608_color_code
{
	COL_WHITE = 0,
	COL_GREEN = 1,
	COL_BLUE = 2,
	COL_CYAN = 3,
	COL_RED = 4,
	COL_YELLOW = 5,
	COL_MAGENTA = 6,
	COL_USERDEFINED = 7,
	COL_BLACK = 8,
	COL_TRANSPARENT = 9,

	// Must keep at end
	COL_MAX
};

struct eia608_screen // A CC buffer
{
	/** format of data inside this structure */
	enum cea_eia608_format format;
	unsigned char characters[CEA_DECODER_608_SCREEN_ROWS][CEA_DECODER_608_SCREEN_WIDTH + 1];
	enum cea_decoder_608_color_code colors[CEA_DECODER_608_SCREEN_ROWS][CEA_DECODER_608_SCREEN_WIDTH + 1];
	enum font_bits fonts[CEA_DECODER_608_SCREEN_ROWS][CEA_DECODER_608_SCREEN_WIDTH + 1]; // Extra char at the end for a 0
	int row_used[CEA_DECODER_608_SCREEN_ROWS];					     // Any data in row?
	int empty;									     // Buffer completely empty?
	/** start time of this CC buffer */
	int64_t start_time;
	/** end time of this CC buffer */
	int64_t end_time;
	enum cc_modes mode;
	int channel;  // Currently selected channel
	int my_field; // Used for sanity checks
};

struct cea_decoders_common_settings_t
{
	int extract; // Extract 1st, 2nd or both fields
	struct cea_decoder_608_settings *settings_608; // Contains the settings for the 608 decoder.
	cea_decoder_dtvcc_settings *settings_dtvcc;    // Same for cea 708 captions decoder (dtvcc)
	int cc_channel;				       // Channel we want to dump in srt mode
};

struct lib_cc_decode
{
	int cc_stats[4];
	int processed_enough; // If 1, we have enough lines, time, etc.

	/* 608 contexts - note that this shouldn't be global, they should be
	per program */
	void *context_cc608_field_1;
	void *context_cc608_field_2;

	int extract;   // Extract 1st, 2nd or both fields

	struct cea_common_timing_ctx *timing;
	dtvcc_ctx *dtvcc;
	int current_field;
	int (*writedata)(const unsigned char *data, int length, void *private_data, struct cc_subtitle *sub);
};

#endif
