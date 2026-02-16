/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_DTVCC_H
#define CEA_DTVCC_H

#include "cea_decoders_708.h"

void dtvcc_process_data(struct dtvcc_ctx *dtvcc,
			const unsigned char *data);

dtvcc_ctx *dtvcc_init(cea_decoder_dtvcc_settings *opts);
void dtvcc_free(dtvcc_ctx **);

#endif // CEA_DTVCC_H
