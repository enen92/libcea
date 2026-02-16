/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_common_timing.h"
#include "cea_common_constants.h"
#include "cea_common_structs.h"
#include "cea_common_common.h"
#include <stdlib.h>


/* Provide the current time since the file (or the first file) started
 * in ms using PTS time information.
 */

// Count 608 (per field) and 708 blocks since last set_fts() call
int cb_field1, cb_field2, cb_708;

int MPEG_CLOCK_FREQ = 90000; // This "constant" is part of the standard

int max_dif = 5;
unsigned pts_big_change;

double current_fps = (double)30000.0 / 1001; /* 29.97 */

int frames_since_ref_time = 0;
unsigned total_frames_count;

struct cea_common_timing_settings_t cea_common_timing_settings;

/* Implemented in timing_impl.c */
void cea_set_current_pts(struct cea_common_timing_ctx *ctx, int64_t pts);
int cea_set_fts(struct cea_common_timing_ctx *ctx);
int64_t cea_get_fts(struct cea_common_timing_ctx *ctx, int current_field);
int64_t cea_get_visible_start(struct cea_common_timing_ctx *ctx, int current_field);
int64_t cea_get_visible_end(struct cea_common_timing_ctx *ctx, int current_field);
char *cea_print_mstime_static(int64_t mstime, char *buf);

void cea_common_timing_init(int64_t *file_position, int no_sync)
{
	cea_common_timing_settings.disable_sync_check = 0;
	cea_common_timing_settings.is_elementary_stream = 0;
	cea_common_timing_settings.file_position = file_position;
	cea_common_timing_settings.no_sync = no_sync;
}

void dinit_timing_ctx(struct cea_common_timing_ctx **arg)
{
	freep(arg);
}
struct cea_common_timing_ctx *init_timing_ctx(struct cea_common_timing_settings_t *cfg)
{
	struct cea_common_timing_ctx *ctx = (struct cea_common_timing_ctx *)malloc(sizeof(struct cea_common_timing_ctx));
	if (!ctx)
		return NULL;

	ctx->pts_set = 0;
	ctx->current_tref = 0;
	ctx->current_pts = 0;
	ctx->current_picture_coding_type = CEA_FRAME_TYPE_RESET_OR_UNKNOWN;
	ctx->min_pts_adjusted = 0;
	ctx->seen_known_frame_type = 0;
	ctx->pending_min_pts = 0x01FFFFFFFFLL;
	ctx->unknown_frame_count = 0;
	ctx->min_pts = 0x01FFFFFFFFLL; // 33 bit
	ctx->max_pts = 0;
	ctx->sync_pts = 0;
	ctx->minimum_fts = 0;
	ctx->sync_pts2fts_set = 0;
	ctx->sync_pts2fts_fts = 0;
	ctx->sync_pts2fts_pts = 0;

	ctx->fts_now = 0;
	ctx->fts_offset = 0;
	ctx->fts_fc_offset = 0;
	ctx->fts_max = 0;
	ctx->fts_global = 0;
	ctx->pts_reset = 0;

	(void)cfg;
	return ctx;
}

void set_current_pts(struct cea_common_timing_ctx *ctx, int64_t pts)
{
	cea_set_current_pts(ctx, pts);
}

int set_fts(struct cea_common_timing_ctx *ctx)
{
	return cea_set_fts(ctx);
}

int64_t get_fts(struct cea_common_timing_ctx *ctx, int current_field)
{
	return cea_get_fts(ctx, current_field);
}

size_t print_mstime_buff(int64_t mstime, char *fmt, char *buf)
{
	unsigned hh, mm, ss, ms;
	int signoffset = (mstime < 0 ? 1 : 0);
	const size_t max_time_len = 32;

	if (mstime < 0)
		mstime = -mstime;

	hh = (unsigned)(mstime / 1000 / 60 / 60);
	mm = (unsigned)(mstime / 1000 / 60 - 60 * hh);
	ss = (unsigned)(mstime / 1000 - 60 * (mm + 60 * hh));
	ms = (unsigned)(mstime - 1000 * (ss + 60 * (mm + 60 * hh)));

	buf[0] = '-';
	return (size_t)snprintf(buf + signoffset, max_time_len, fmt, hh, mm, ss, ms);
}

char *print_mstime_static(int64_t mstime)
{
	static char buf[15];
	return cea_print_mstime_static(mstime, buf);
}
