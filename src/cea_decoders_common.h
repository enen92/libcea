/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef _CEA_DECODER_COMMON
#define _CEA_DECODER_COMMON

#include "cea_common_constants.h"
#include "cea_common_structs.h"
#include "cea_decoders_structs.h"

#include <stdint.h>


int64_t get_visible_start(struct cea_common_timing_ctx *ctx, int current_field);
int64_t get_visible_end(struct cea_common_timing_ctx *ctx, int current_field);

int validate_cc_data_pair(unsigned char *cc_data_pair);
int process_cc_data(struct lib_cc_decode *ctx, unsigned char *cc_data, int cc_count, struct cc_subtitle *sub);
int do_cb(struct lib_cc_decode *ctx, unsigned char *cc_block, struct cc_subtitle *sub);
void printdata(struct lib_cc_decode *ctx, const unsigned char *data1, int length1,
	       const unsigned char *data2, int length2, struct cc_subtitle *sub);
struct lib_cc_decode *init_cc_decode(struct cea_decoders_common_settings_t *setting);
void dinit_cc_decode(struct lib_cc_decode **ctx);
void flush_cc_decode(struct lib_cc_decode *ctx, struct cc_subtitle *sub);

#endif
