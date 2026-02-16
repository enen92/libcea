/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_common_timing.h"
#include "cea_common_common.h"
#include <stdio.h>
#include <string.h>

/* External globals defined in cea_common_timing.c */
extern int cb_field1, cb_field2, cb_708;
extern int MPEG_CLOCK_FREQ;
extern int max_dif;
extern unsigned pts_big_change;
extern double current_fps;
extern int frames_since_ref_time;
extern unsigned total_frames_count;
extern struct cea_common_timing_settings_t cea_common_timing_settings;

/* Helper: convert MPEG clock ticks to milliseconds */
static int64_t ticks_to_ms(int64_t ticks, int clock_freq)
{
	if (clock_freq == 0) return 0;
	return (ticks * 1000) / clock_freq;
}

/* Helper: convert frame count to milliseconds at given fps */
static int64_t frames_to_ms(int frames, double fps)
{
	if (fps <= 0.0) return 0;
	return (int64_t)((double)frames * 1000.0 / fps);
}

/* Helper: convert frame count to MPEG clock ticks */
static int64_t frames_to_ticks(int frames, double fps, int clock_freq)
{
	if (fps <= 0.0) return 0;
	return (int64_t)((double)frames * (double)clock_freq / fps);
}

/*
 * set_current_pts: Store PTS value, detect resets
 */
void cea_set_current_pts(struct cea_common_timing_ctx *ctx, int64_t pts)
{
	int64_t prev_pts = ctx->current_pts;
	ctx->current_pts = pts;

	if (ctx->pts_set == 0) /* PTS_SET_NO */
		ctx->pts_set = 1; /* PTS_SET_RECEIVED */

	/* Check for PTS reset (current < previous) */
	if (ctx->current_pts < prev_pts)
		ctx->pts_reset = 1;
}

/*
 * set_fts: The core timing synchronization function.
 * Returns 1 on success, 0 on error.
 *
 * Ported from TimingContext::set_fts() in timing.rs
 */
int cea_set_fts(struct cea_common_timing_ctx *ctx)
{
	int pts_jump = 0;

	/* Phase 1: Elementary stream with no PTS yet */
	if (ctx->pts_set == 0 && cea_common_timing_settings.is_elementary_stream)
		return 1;

	/* Phase 2: PTS jump detection */
	if (ctx->pts_set == 2 && !cea_common_timing_settings.disable_sync_check) /* MinPtsSet */
	{
		int64_t dif_ticks = ctx->current_pts - ctx->sync_pts;
		int64_t dif_sec = ticks_to_ms(dif_ticks, MPEG_CLOCK_FREQ) / 1000;

		if (dif_sec < 0 || dif_sec > max_dif)
		{
			pts_jump = 1;
			pts_big_change = 1;

			/* If not at GOP start, can't fully resync -- estimate and return */
			if (ctx->current_tref != 0 ||
			    ctx->current_picture_coding_type != CEA_FRAME_TYPE_I_FRAME)
			{
				ctx->fts_now = ctx->fts_max;
				return 1;
			}
		}
	}

	/* Phase 3: PTS rollover compensation */
	if (ctx->pts_set == 2 && !ctx->min_pts_adjusted)
	{
		int cur_bits = (int)((ctx->current_pts >> 30) & 0x07);
		int min_bits = (int)((ctx->min_pts >> 30) & 0x07);

		if (cur_bits == 7 && min_bits == 0)
		{
			ctx->min_pts = ctx->current_pts;
			ctx->min_pts_adjusted = 1;
		}
		else if (cur_bits >= 1 && cur_bits <= 6)
		{
			ctx->min_pts_adjusted = 1;
		}
	}

	/* Phase 4: Set min_pts */
	if (ctx->pts_set != 0)
	{
		#define FALLBACK_THRESHOLD 100
		#define GARBAGE_GAP_THRESHOLD_MS 100

		int allow_min_pts_set = 0;
		int64_t pts_for_min = ctx->current_pts;

		/* Track frame type knowledge */
		if (ctx->current_picture_coding_type != CEA_FRAME_TYPE_RESET_OR_UNKNOWN &&
		    !ctx->seen_known_frame_type)
		{
			ctx->seen_known_frame_type = 1;
		}

		/* Track minimum PTS */
		if (ctx->current_pts < ctx->pending_min_pts)
			ctx->pending_min_pts = ctx->current_pts;

		if (ctx->current_picture_coding_type == CEA_FRAME_TYPE_RESET_OR_UNKNOWN)
			ctx->unknown_frame_count++;

		/* Decide whether to set min_pts */
		switch (ctx->current_picture_coding_type)
		{
		case CEA_FRAME_TYPE_RESET_OR_UNKNOWN:
			if (ctx->unknown_frame_count >= FALLBACK_THRESHOLD &&
			    !ctx->seen_known_frame_type &&
			    ctx->pending_min_pts != 0x01FFFFFFFFLL)
			{
				allow_min_pts_set = 1;
				pts_for_min = ctx->pending_min_pts;
			}
			break;

		case CEA_FRAME_TYPE_I_FRAME:
			if (ctx->pending_min_pts != 0x01FFFFFFFFLL)
			{
				int64_t gap_ticks = ctx->current_pts - ctx->pending_min_pts;
				int64_t gap_ms = ticks_to_ms(gap_ticks, MPEG_CLOCK_FREQ);
				if (gap_ms > GARBAGE_GAP_THRESHOLD_MS)
				{
					/* Large gap = garbage leading frames */
					allow_min_pts_set = 1;
					pts_for_min = ctx->current_pts;
				}
				else
				{
					/* Small gap = valid B-frames */
					allow_min_pts_set = 1;
					pts_for_min = ctx->pending_min_pts;
				}
			}
			else
			{
				allow_min_pts_set = 1;
				pts_for_min = ctx->current_pts;
			}
			break;

		default: /* B-frame, P-frame -- don't set min_pts */
			break;
		}

		/* Actually set min_pts if conditions met */
		if (pts_for_min < ctx->min_pts &&
		    !pts_jump &&
		    ctx->min_pts == 0x01FFFFFFFFLL &&
		    allow_min_pts_set)
		{
			ctx->min_pts = pts_for_min;
			ctx->pts_set = 2; /* MinPtsSet */

			/* Calculate sync_pts (PTS at GOP start, tref=0) */
			ctx->sync_pts = ctx->current_pts -
					frames_to_ticks(ctx->current_tref, current_fps, MPEG_CLOCK_FREQ);

			/* Calculate fts_offset (time before first sync_pts) */
			if (ctx->current_tref == 0 ||
			    ((int)total_frames_count - frames_since_ref_time) == 0)
			{
				ctx->fts_offset = 0;
			}
			else
			{
				ctx->fts_offset = frames_to_ms(
					(int)total_frames_count - frames_since_ref_time + 1,
					current_fps);
			}
		}
	}

	/* Phase 5: Handle PTS jump (after min_pts is set) */
	if (pts_jump && !cea_common_timing_settings.no_sync)
	{
		ctx->fts_offset = ctx->fts_offset +
				  ticks_to_ms(ctx->sync_pts - ctx->min_pts, MPEG_CLOCK_FREQ) +
				  frames_to_ms(frames_since_ref_time, current_fps);
		ctx->fts_max = ctx->fts_offset;

		/* Reset sync tracking for new timeline */
		ctx->sync_pts2fts_set = 0;

		/* Set new sync_pts accounting for temporal reference offset */
		ctx->sync_pts = ctx->current_pts -
				frames_to_ticks(ctx->current_tref, current_fps, MPEG_CLOCK_FREQ);

		/* Set min_pts = sync_pts to enable fts_now calculation */
		ctx->min_pts = ctx->sync_pts;
		ctx->pts_set = 2; /* MinPtsSet */
	}

	/* Phase 6: Update sync_pts at GOP start */
	if (ctx->current_tref == 0)
		ctx->sync_pts = ctx->current_pts;

	/* Phase 7: Reset caption counters */
	cb_field1 = 0;
	cb_field2 = 0;
	cb_708 = 0;

	/* Phase 8: Calculate fts_now */
	if (ctx->pts_set == 2) /* MinPtsSet */
	{
		ctx->fts_now = ticks_to_ms(ctx->current_pts - ctx->min_pts, MPEG_CLOCK_FREQ) +
			       ctx->fts_offset;

		if (!ctx->sync_pts2fts_set)
		{
			ctx->sync_pts2fts_pts = ctx->current_pts;
			ctx->sync_pts2fts_fts = ctx->fts_now;
			ctx->sync_pts2fts_set = 1;
		}
	}
	else if (ctx->pts_set == 0) /* No PTS */
	{
		return 0;
	}
	/* else pts_set == 1 (Received): keep previous fts_now */

	/* Phase 9: Update fts_max */
	if (ctx->fts_now > ctx->fts_max)
		ctx->fts_max = ctx->fts_now;

	/* Phase 10: Handle PTS reset */
	if (ctx->pts_reset)
	{
		ctx->minimum_fts = 0;
		ctx->fts_max = ctx->fts_now;
		ctx->pts_reset = 0;
	}

	return 1;
}

/*
 * get_fts: Return current FTS including field offset
 */
int64_t cea_get_fts(struct cea_common_timing_ctx *ctx, int current_field)
{
	int count;

	switch (current_field)
	{
	case 1:
		count = cb_field1;
		break;
	case 2:
		count = cb_field2;
		break;
	case 3:
		count = cb_708;
		break;
	default:
		count = 0;
		break;
	}

	/* 1001/30 ms per caption block (assumes 29.97 fps) */
	return ctx->fts_now + ctx->fts_global + (int64_t)count * 1001 / 30;
}

/*
 * get_visible_start: Returns FTS guaranteed >= previous end + 1
 */
int64_t cea_get_visible_start(struct cea_common_timing_ctx *ctx, int current_field)
{
	(void)current_field;
	int64_t fts = ctx->fts_now + ctx->fts_global;

	if (fts <= ctx->minimum_fts)
		return ctx->minimum_fts + 1;
	else
		return fts;
}

/*
 * get_visible_end: Returns current FTS and updates minimum_fts tracking
 */
int64_t cea_get_visible_end(struct cea_common_timing_ctx *ctx, int current_field)
{
	(void)current_field;
	int64_t fts = ctx->fts_now + ctx->fts_global;

	if (fts > ctx->minimum_fts)
		ctx->minimum_fts = fts;

	return fts;
}

/*
 * print_mstime_static: Format time as HH:MM:SS:mmm
 */
char *cea_print_mstime_static(int64_t mstime, char *buf)
{
	unsigned hh, mm, ss, ms;
	int sign = 0;

	if (mstime < 0)
	{
		sign = 1;
		mstime = -mstime;
	}

	hh = (unsigned)(mstime / 1000 / 60 / 60);
	mm = (unsigned)(mstime / 1000 / 60 - 60 * hh);
	ss = (unsigned)(mstime / 1000 - 60 * (mm + 60 * hh));
	ms = (unsigned)(mstime - 1000 * (ss + 60 * (mm + 60 * hh)));

	if (sign)
		snprintf(buf, 15, "-%02u:%02u:%02u:%03u", hh, mm, ss, ms);
	else
		snprintf(buf, 15, "%02u:%02u:%02u:%03u", hh, mm, ss, ms);

	return buf;
}
