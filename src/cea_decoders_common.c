/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_decoders_common.h"
#include "cea_common_structs.h"
#include "cea_common_char_encoding.h"
#include "cea_common_constants.h"
#include "cea_common_timing.h"
#include "cea_common_common.h"
#include "cea_decoders_608.h"
#include "cea_decoders_708.h"
#include "cea_dtvcc.h"

#include <stdlib.h>
#include <string.h>

int64_t cea_get_visible_start(struct cea_common_timing_ctx *ctx, int current_field);
int64_t cea_get_visible_end(struct cea_common_timing_ctx *ctx, int current_field);

int64_t get_visible_start(struct cea_common_timing_ctx *ctx, int current_field)
{
	int64_t fts = cea_get_visible_start(ctx, current_field);
	dbg_print(CEA_DMT_DECODER_608, "Visible Start time=%s\n", print_mstime_static(fts));
	return fts;
}

int64_t get_visible_end(struct cea_common_timing_ctx *ctx, int current_field)
{
	int64_t fts = cea_get_visible_end(ctx, current_field);
	dbg_print(CEA_DMT_DECODER_608, "Visible End time=%s\n", print_mstime_static(fts));
	return fts;
}

/* Modified: removed encoder_ctx parameter, removed MCC path, uses C DTVCC */
int process_cc_data(struct lib_cc_decode *dec_ctx, unsigned char *cc_data, int cc_count, struct cc_subtitle *sub)
{
	int ret = -1;

	/* Process DTVCC (708) data directly in C.
	 * Note: dtvcc->current_sub must be set by the caller so that 708
	 * output goes to a separate subtitle chain from 608.  This avoids
	 * memory corruption when both decoders produce output in the same
	 * call (608 uses realloc on sub->data while 708 uses add_cc_sub_text). */
	if (dec_ctx->dtvcc && dec_ctx->dtvcc->is_active)
	{
		for (int j = 0; j < cc_count * 3; j += 3)
		{
			unsigned char cc_valid = (cc_data[j] & 4) >> 2;
			unsigned char cc_type = cc_data[j] & 3;
			if ((cc_valid || cc_type == 3) && cc_type >= 2)
			{
				unsigned char dtvcc_data[4];
				dtvcc_data[0] = cc_valid;
				dtvcc_data[1] = cc_type;
				dtvcc_data[2] = cc_data[j + 1];
				dtvcc_data[3] = cc_data[j + 2];
				dtvcc_process_data(dec_ctx->dtvcc, dtvcc_data);
			}
		}
	}

	/* Process 608 data */
	for (int j = 0; j < cc_count * 3; j = j + 3)
	{
		if (validate_cc_data_pair(cc_data + j))
			continue;
		ret = do_cb(dec_ctx, cc_data + j, sub);
		if (ret == 1)
			ret = 0;
	}
	return ret;
}

int validate_cc_data_pair(unsigned char *cc_data_pair)
{
	unsigned char cc_valid = (*cc_data_pair & 4) >> 2;
	unsigned char cc_type = *cc_data_pair & 3;

	if (!cc_valid)
		return -1;

	if (cc_type == 0 || cc_type == 1)
	{
		if (!cc608_parity_table[cc_data_pair[2]])
		{
			return -1;
		}
		if (!cc608_parity_table[cc_data_pair[1]])
		{
			cc_data_pair[1] = 0x7F;
		}
	}
	return 0;
}

int do_cb(struct lib_cc_decode *ctx, unsigned char *cc_block, struct cc_subtitle *sub)
{
	unsigned char cc_valid = (*cc_block & 4) >> 2;
	unsigned char cc_type = *cc_block & 3;

	if ((cc_block[0] == 0xFA || cc_block[0] == 0xFC || cc_block[0] == 0xFD) && (cc_block[1] & 0x7F) == 0 && (cc_block[2] & 0x7F) == 0)
		return 1;

	dbg_print(CEA_DMT_CBRAW, "%s   %02X:%c%c:%02X", print_mstime_static(ctx->timing->fts_now + ctx->timing->fts_global),
		  cc_block[0], cc_block[1] & 0x7f, cc_block[2] & 0x7f, cc_block[2]);

	if (cc_valid || cc_type == 3)
	{
		ctx->cc_stats[cc_type]++;

		switch (cc_type)
		{
			case 0:
				dbg_print(CEA_DMT_CBRAW, "    %s   ..   ..\n", debug_608_to_ASC(cc_block, 0));
				ctx->current_field = 1;
				printdata(ctx, cc_block + 1, 2, 0, 0, sub);
				cb_field1++;
				break;
			case 1:
				dbg_print(CEA_DMT_CBRAW, "    ..   %s   ..\n", debug_608_to_ASC(cc_block, 1));
				ctx->current_field = 2;
				printdata(ctx, 0, 0, cc_block + 1, 2, sub);
				cb_field2++;
				break;
			case 2:
			case 3:
				dbg_print(CEA_DMT_CBRAW, "    ..   ..   DD\n");
				ctx->current_field = 3;
				cb_708++;
				break;
			default:
				fatal(CEA_COMMON_EXIT_BUG_BUG, "In do_cb: Impossible value for cc_type.\n");
		}
	}
	else
	{
		dbg_print(CEA_DMT_CBRAW, "    ..   ..   ..\n");
		dbg_print(CEA_DMT_VERBOSE, "Found !(cc_valid || cc_type==3) - ignore this block\n");
	}

	return 1;
}

void dinit_cc_decode(struct lib_cc_decode **ctx)
{
	struct lib_cc_decode *lctx = *ctx;
	dtvcc_free(&lctx->dtvcc);
	cea_decoder_608_dinit_library(&lctx->context_cc608_field_1);
	cea_decoder_608_dinit_library(&lctx->context_cc608_field_2);
	dinit_timing_ctx(&lctx->timing);
	freep(ctx);
}

struct lib_cc_decode *init_cc_decode(struct cea_decoders_common_settings_t *setting)
{
	struct lib_cc_decode *ctx = NULL;

	ctx = (struct lib_cc_decode *)malloc(sizeof(struct lib_cc_decode));
	if (!ctx)
		fatal(EXIT_NOT_ENOUGH_MEMORY, "In init_cc_decode: Out of memory allocating ctx.");

	memset(ctx, 0, sizeof(*ctx));

	ctx->timing = init_timing_ctx(&cea_common_timing_settings);
	if (!ctx->timing)
		fatal(EXIT_NOT_ENOUGH_MEMORY, "In init_cc_decode: Out of memory initializing timing.");

	setting->settings_dtvcc->timing = ctx->timing;

	/* Always use C dtvcc */
	ctx->dtvcc = dtvcc_init(setting->settings_dtvcc);
	if (!ctx->dtvcc)
		fatal(EXIT_NOT_ENOUGH_MEMORY, "In init_cc_decode: Out of memory initializing dtvcc.");
	ctx->dtvcc->is_active = setting->settings_dtvcc->enabled;

	ctx->context_cc608_field_1 = cea_decoder_608_init_library(
		setting->settings_608,
		setting->cc_channel,
		1,
		&ctx->processed_enough,
		0, /* cc_to_stdout */
		ctx->timing);
	if (!ctx->context_cc608_field_1)
		fatal(EXIT_NOT_ENOUGH_MEMORY, "In init_cc_decode: Out of memory initializing context_cc608_field_1.");
	ctx->context_cc608_field_2 = cea_decoder_608_init_library(
		setting->settings_608,
		setting->cc_channel,
		2,
		&ctx->processed_enough,
		0, /* cc_to_stdout */
		ctx->timing);
	if (!ctx->context_cc608_field_2)
		fatal(EXIT_NOT_ENOUGH_MEMORY, "In init_cc_decode: Out of memory initializing context_cc608_field_2.");

	ctx->current_field = 1;
	ctx->extract = setting->extract;

	/* Always use process608 for writedata in lite */
	ctx->writedata = process608;

	return ctx;
}

void flush_cc_decode(struct lib_cc_decode *ctx, struct cc_subtitle *sub)
{
	if (ctx->extract != 2)
	{
		flush_608_context(ctx->context_cc608_field_1, sub);
	}
	if (ctx->extract != 1)
	{
		flush_608_context(ctx->context_cc608_field_2, sub);
	}

	if (ctx->dtvcc && ctx->dtvcc->is_active)
	{
		/* dtvcc->current_sub must already be set by the caller */
		for (int i = 0; i < CEA_DTVCC_MAX_SERVICES; i++)
		{
			dtvcc_service_decoder *decoder = &ctx->dtvcc->decoders[i];
			if (!ctx->dtvcc->services_active[i])
				continue;
			if (decoder->cc_count > 0)
			{
				ctx->current_field = 3;
				dtvcc_decoder_flush(ctx->dtvcc, decoder);
			}
		}
	}
}
