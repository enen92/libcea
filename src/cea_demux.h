/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef CEA_DEMUX_H
#define CEA_DEMUX_H

#include <stdint.h>

/*
 * Result from a demuxer extract call.
 */
typedef struct {
	int cc_count;        /* Number of 3-byte triplets written to cc_out (0 = none) */
	int reorder_window;  /* -1 = no update; >= 0 = stream-detected reorder window */
} cea_demux_result;

/*
 * Extract cc_data from an H.264 packet. Handles Annex B and AVCC formats.
 *
 * is_avcc:          1 for AVCC (length-prefixed NALs), 0 for Annex B
 * nal_length_size:  in/out -- 0 triggers auto-detection for AVCC, then cached
 * data/size:        raw packet data
 * cc_out:           output buffer, must hold at least 93 bytes (31*3)
 */
cea_demux_result cea_demux_h264_extract_cc(int is_avcc, int *nal_length_size,
                                           const uint8_t *data, int size,
                                           uint8_t *cc_out);

/*
 * Extract cc_data from an MPEG-2 video packet.
 *
 * data/size: raw packet data
 * cc_out:    output buffer, must hold at least 93 bytes (31*3)
 */
cea_demux_result cea_demux_mpeg2_extract_cc(const uint8_t *data, int size,
                                            uint8_t *cc_out);

/*
 * Parse H.264 extradata (Annex B or AVCC format) for max_num_reorder_frames.
 * Returns the value (>= 0) on success, or -1 if not found/parse error.
 */
int cea_demux_h264_parse_extradata_reorder(const uint8_t *extradata, int size);

#endif /* CEA_DEMUX_H */
