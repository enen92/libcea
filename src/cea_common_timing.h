/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_COMMON_TIMING_H
#define CEA_COMMON_TIMING_H

#include "cea_common_constants.h"

struct cea_common_timing_settings_t
{
	int disable_sync_check;	  // If 1, timeline jumps will be ignored. This is important in several input formats that are assumed to have correct timing, no matter what.
	int no_sync;		  // If 1, there will be no sync at all. Mostly useful for debugging.
	int is_elementary_stream; // Needs to be set, as it's used in set_fts.
	int64_t *file_position;	  // The position of the file
};
extern struct cea_common_timing_settings_t cea_common_timing_settings;

struct cea_common_timing_ctx
{
	int pts_set;			  // 0 = No, 1 = received, 2 = min_pts set
	int min_pts_adjusted;		  // 0 = No, 1=Yes (don't adjust again)
	int seen_known_frame_type;	  // 0 = No, 1 = Yes. Tracks if we've seen a frame with known type
	int64_t pending_min_pts;		  // Minimum PTS seen while waiting for frame type determination
	unsigned int unknown_frame_count; // Count of set_fts calls with unknown frame type
	int64_t current_pts;
	enum cea_frame_type current_picture_coding_type;
	int current_tref; // Store temporal reference of current frame
	int64_t min_pts;
	int64_t max_pts;
	int64_t sync_pts;
	int64_t minimum_fts;    // No screen should start before this FTS
	int64_t fts_now;	      // Time stamp of current file (w/ fts_offset, w/o fts_global)
	int64_t fts_offset;     // Time before first sync_pts
	int64_t fts_fc_offset;  // Time before first GOP
	int64_t fts_max;	      // Remember the maximum fts that we saw in current file
	int64_t fts_global;     // Duration of previous files (-ve mode)
	int sync_pts2fts_set; // 0 = No, 1 = Yes
	int64_t sync_pts2fts_fts;
	int64_t sync_pts2fts_pts;
	int pts_reset; // 0 = No, 1 = Yes. PTS resets when current_pts is lower than prev
};

// Count 608 (per field) and 708 blocks since last set_fts() call
extern int cb_field1, cb_field2, cb_708;

void cea_common_timing_init(int64_t *file_position, int no_sync);

void dinit_timing_ctx(struct cea_common_timing_ctx **arg);
struct cea_common_timing_ctx *init_timing_ctx(struct cea_common_timing_settings_t *cfg);

void set_current_pts(struct cea_common_timing_ctx *ctx, int64_t pts);
int set_fts(struct cea_common_timing_ctx *ctx);
int64_t get_fts(struct cea_common_timing_ctx *ctx, int current_field);
char *print_mstime_static(int64_t mstime);
size_t print_mstime_buff(int64_t mstime, char *fmt, char *buf);

#endif
