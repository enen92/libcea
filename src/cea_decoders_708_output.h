/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_DECODERS_708_OUTPUT_H
#define CEA_DECODERS_708_OUTPUT_H

#include "cea_decoders_708.h"
#include "cea_common_structs.h"

/* Extract all text from a 708 screen into a cc_subtitle chain.
   Returns 0 on success, -1 on error. */
int dtvcc_screen_to_subtitle(dtvcc_tv_screen *tv, struct cc_subtitle *sub);

#endif /*CEA_DECODERS_708_OUTPUT_H*/
