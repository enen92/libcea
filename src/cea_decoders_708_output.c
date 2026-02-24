/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_decoders_708_output.h"
#include "cea_decoders_708.h"
#include "cea_common_common.h"
#include "cea_common_constants.h"
#include "cea_common_structs.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int dtvcc_is_row_empty(dtvcc_tv_screen *tv, int row_index)
{
	for (int j = 0; j < CEA_DTVCC_SCREENGRID_COLUMNS; j++)
	{
		if (CEA_DTVCC_SYM_IS_SET(tv->chars[row_index][j]))
			return 0;
	}
	return 1;
}

static int dtvcc_is_screen_empty_lite(dtvcc_tv_screen *tv)
{
	for (int i = 0; i < CEA_DTVCC_SCREENGRID_ROWS; i++)
	{
		if (!dtvcc_is_row_empty(tv, i))
			return 0;
	}
	return 1;
}

static void dtvcc_get_write_interval(dtvcc_tv_screen *tv, int row_index, int *first, int *last)
{
	for (*first = 0; *first < CEA_DTVCC_SCREENGRID_COLUMNS; (*first)++)
		if (CEA_DTVCC_SYM_IS_SET(tv->chars[row_index][*first]))
			break;
	for (*last = CEA_DTVCC_SCREENGRID_COLUMNS - 1; *last > 0; (*last)--)
		if (CEA_DTVCC_SYM_IS_SET(tv->chars[row_index][*last]))
			break;
}

/* Encode a Unicode code point (16-bit) as UTF-8.
   Returns number of bytes written (1-3). */
static int encode_utf8(unsigned short cp, char *out)
{
	if (cp < 0x80)
	{
		out[0] = (char)cp;
		return 1;
	}
	else if (cp < 0x800)
	{
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	else
	{
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
}

/* Convert a 708 6-bit color (2 bits each R,G,B) to an HTML hex string.
   Returns NULL for white (0x3F) = default. buf must be at least 8 bytes. */
static const char *color_708_hex(int color, char *buf)
{
	static const unsigned char tbl[] = { 0x00, 0x55, 0xAA, 0xFF };
	if (color == 0x3F) /* white = default */
		return NULL;
	unsigned char r = tbl[(color >> 4) & 3];
	unsigned char g = tbl[(color >> 2) & 3];
	unsigned char b = tbl[color & 3];
	sprintf(buf, "#%02X%02X%02X", r, g, b);
	return buf;
}

/* Extract all text from a 708 screen into a cc_subtitle.
   Text from multiple rows is joined with '\n'.
   Includes SRT-style <i>, <u>, <font color> tags for styling. */
int dtvcc_screen_to_subtitle(dtvcc_tv_screen *tv, struct cc_subtitle *sub)
{
	if (dtvcc_is_screen_empty_lite(tv))
		return 0;

	/* Allocate generously: styled text can be much larger than plain */
	size_t buf_capacity = CEA_DTVCC_SCREENGRID_ROWS * CEA_DTVCC_SCREENGRID_COLUMNS * 60 + 256;
	char *buf = (char *)malloc(buf_capacity);
	if (!buf)
		return -1;

	size_t buf_len = 0;
	int rows_written = 0;
	int bottom_row = -1;

	for (int i = 0; i < CEA_DTVCC_SCREENGRID_ROWS; i++)
	{
		if (dtvcc_is_row_empty(tv, i))
			continue;

		bottom_row = i;

		if (rows_written > 0)
			buf[buf_len++] = '\n';

		int first, last;
		dtvcc_get_write_interval(tv, i, &first, &last);

		/* Track current style state for this row */
		int cur_fg = 0x3F; /* white = default = no tag */
		int has_font_tag = 0;
		int cur_italic = 0;
		int cur_underline = 0;
		char color_buf[8];

		for (int j = first; j <= last; j++)
		{
			int want_italic = tv->pen_attribs[i][j].italic;
			int want_underline = tv->pen_attribs[i][j].underline;
			int want_fg = tv->pen_colors[i][j].fg_color;

			/* Close tags if style changed (reverse order) */
			if (cur_underline && !want_underline)
			{ memcpy(buf + buf_len, "</u>", 4); buf_len += 4; cur_underline = 0; }
			if (cur_italic && !want_italic)
			{ memcpy(buf + buf_len, "</i>", 4); buf_len += 4; cur_italic = 0; }
			if (has_font_tag && want_fg != cur_fg)
			{ memcpy(buf + buf_len, "</font>", 7); buf_len += 7; has_font_tag = 0; }

			/* Open tags if needed */
			if (want_fg != 0x3F && !has_font_tag)
			{
				const char *hex = color_708_hex(want_fg, color_buf);
				if (hex)
				{
					buf_len += sprintf(buf + buf_len, "<font color=\"%s\">", hex);
					has_font_tag = 1;
				}
			}
			cur_fg = want_fg;
			if (want_italic && !cur_italic)
			{ memcpy(buf + buf_len, "<i>", 3); buf_len += 3; cur_italic = 1; }
			if (want_underline && !cur_underline)
			{ memcpy(buf + buf_len, "<u>", 3); buf_len += 3; cur_underline = 1; }

			/* Write the character */
			if (CEA_DTVCC_SYM_IS_SET(tv->chars[i][j]))
				buf_len += encode_utf8(tv->chars[i][j].sym, buf + buf_len);
			else
				buf[buf_len++] = ' ';
		}

		/* Close any remaining open tags at end of row */
		if (cur_underline)
		{ memcpy(buf + buf_len, "</u>", 4); buf_len += 4; }
		if (cur_italic)
		{ memcpy(buf + buf_len, "</i>", 4); buf_len += 4; }
		if (has_font_tag)
		{ memcpy(buf + buf_len, "</font>", 7); buf_len += 7; }

		rows_written++;
	}

	if (rows_written == 0)
	{
		free(buf);
		return 0;
	}

	buf[buf_len] = '\0';

	/* Encode service number into info: "7XX" (e.g. "701" = service 1).
	 * collect_captions decodes this back into cea_caption.channel. */
	char info_str[4];
	snprintf(info_str, sizeof(info_str), "7%02d", tv->service_number);
	int ret = add_cc_sub_text(sub, buf, tv->time_ms_show, tv->time_ms_hide,
				  info_str, "POP");

	/* Store bottom row on the node that add_cc_sub_text just wrote.
	 * It's always the tail of the chain. */
	if (ret == 0)
	{
		struct cc_subtitle *tail = sub;
		while (tail->next)
			tail = tail->next;
		tail->flags = bottom_row;
	}

	free(buf);
	return ret;
}
