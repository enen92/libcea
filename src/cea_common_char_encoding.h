/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef __CEA_COMMON_CHAR_ENCODING_H__
#define __CEA_COMMON_CHAR_ENCODING_H__

void get_char_in_latin_1(unsigned char *buffer, unsigned char c);
void get_char_in_unicode(unsigned char *buffer, unsigned char c);
int get_char_in_utf_8(unsigned char *buffer, unsigned char c);
unsigned char cctolower(unsigned char c);
unsigned char cctoupper(unsigned char c);

#endif