/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_H
#define CEA_H

#include <stdint.h>
#include "cea_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the library version string (e.g. "0.1.0") */
const char *cea_version(void);

/* Log levels */
typedef enum {
	CEA_LOG_DEBUG   = 0,
	CEA_LOG_INFO    = 1,
	CEA_LOG_WARNING = 2,
	CEA_LOG_ERROR   = 3,
	CEA_LOG_FATAL   = 4,
} cea_log_level;

/* Log callback: receives level, formatted message, and user-supplied pointer */
typedef void (*cea_log_callback)(cea_log_level level, const char *msg, void *userdata);

/*
 * Register a global log callback. Messages below min_level are silently dropped.
 * Pass NULL as cb to disable logging (the default).
 */
void cea_set_log_callback(cea_log_callback cb, void *userdata, cea_log_level min_level);

/* Opaque context */
typedef struct cea_ctx cea_ctx;

/* Caption output */
typedef struct {
	const char *text;   /* UTF-8 caption text (one line per row, \n separated) */
	int64_t start_ms;   /* Start time in milliseconds */
	int64_t end_ms;     /* End time in milliseconds */
	int field;          /* 1=CC1, 2=CC2, 3=CEA-708 */
	int base_row;       /* Bottom-most screen row with content (0-14 for 608, -1 if unknown) */
	char mode[5];       /* Caption mode: "POP", "RU2", "RU3", "RU4", "PAI", "TXT" */
	char info[4];       /* Decoder info: "608" or "708" */
} cea_caption;

/* Options for initialization */
typedef struct {
	int cc_channel;         /* 608 channel: 1=CC1 (default), 2=CC2 */
	int enable_708;         /* Enable CEA-708 decoding (default: 1) */
	int services_708[63];   /* Which 708 services to enable (1-indexed, 0=disabled) */
	int no_rollup;          /* If 1, write one line at a time */
	int reorder_window;     /* B-frame reorder window for cea_feed_packet().
	                         * 0 = auto-detect from stream (default).
	                         * >0 = override with this value.
	                         * The library tries SPS max_num_reorder_frames first,
	                         * then falls back to an SPS-based heuristic, then to
	                         * this value (default 4 if left at 0). */
} cea_options;

/* Initialize with default options */
cea_ctx *cea_init(const cea_options *opts);

/* Initialize with defaults (CC1 + 708 service 1) */
cea_ctx *cea_init_default(void);

/* Cleanup */
void cea_free(cea_ctx *ctx);

/*
 * Feed cc_data triplets with PTS timing.
 * cc_data: array of 3-byte triplets (cc_valid|cc_type, byte1, byte2)
 * cc_count: number of triplets
 * pts_ms: presentation timestamp in milliseconds
 * Returns 0 on success, negative on error.
 */
int cea_feed(cea_ctx *ctx, const unsigned char *cc_data, int cc_count, int64_t pts_ms);

/* Flush remaining buffered captions */
int cea_flush(cea_ctx *ctx);

/* Codec types for demuxer configuration */
typedef enum {
	CEA_CODEC_MPEG2,
	CEA_CODEC_H264,
} cea_codec_type;

/* Packaging formats for compressed video */
typedef enum {
	CEA_PACKAGING_ANNEX_B,  /* Start-code delimited (MPEG-2 always uses this) */
	CEA_PACKAGING_AVCC,     /* Length-prefixed NAL units (H.264 in MP4/MKV) */
} cea_packaging_type;

/*
 * Configure the demuxer. Must be called before cea_feed_packet().
 * extradata/extradata_size: optional codec extradata (SPS/PPS for H.264).
 *   Pass NULL/0 if not available — the library will try to parse it
 *   from the stream, falling back to a default reorder window of 4.
 * Returns 0 on success, negative on error (e.g. MPEG-2 + AVCC).
 */
int cea_set_demuxer(cea_ctx *ctx, cea_codec_type codec,
                    cea_packaging_type packaging,
                    const unsigned char *extradata, int extradata_size);

/*
 * Feed a compressed video packet. Internally extracts cc_data, handles
 * B-frame reordering, and decodes captions. Packets can arrive in decode
 * (DTS) order -- the library reorders by PTS internally.
 * Returns 0 on success, negative on error.
 */
int cea_feed_packet(cea_ctx *ctx, const unsigned char *pkt_data,
                         int pkt_size, int64_t pts_ms);

/*
 * Retrieve decoded captions. Call after feed/flush.
 * out: array to fill with caption entries
 * max_captions: size of out array
 * Returns number of captions written to out (0 if none available).
 * Caller must NOT free the text pointers -- they are valid until the
 * next call to cea_feed, cea_flush, or cea_free.
 */
int cea_get_captions(cea_ctx *ctx, cea_caption *out, int max_captions);

#ifdef __cplusplus
}
#endif

#endif /* CEA_H */
