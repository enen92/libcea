/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_DECODERS_708_ENCODING_H
#define CEA_DECODERS_708_ENCODING_H

#define CEA_DTVCC_MUSICAL_NOTE_CHAR 9836 // Unicode Character 'BEAMED SIXTEENTH NOTES'

extern unsigned char dtvcc_get_internal_from_G0(unsigned char g0_char);
extern unsigned char dtvcc_get_internal_from_G1(unsigned char g1_char);
extern unsigned char dtvcc_get_internal_from_G2(unsigned char g2_char);
extern unsigned char dtvcc_get_internal_from_G3(unsigned char g3_char);

#endif /*CEA_DECODERS_708_ENCODING_H*/
