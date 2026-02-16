/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_demux.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Emulation prevention byte removal for H.264 NAL units               */
/* Converts 00 00 03 xx -> 00 00 xx (removes the 0x03 EPB)            */
/* Returns new length.                                                  */
/* ------------------------------------------------------------------ */
static int remove_epb(const uint8_t *src, int src_len, uint8_t *dst)
{
	int di = 0;
	int i = 0;
	while (i < src_len) {
		if (i + 2 < src_len && src[i] == 0x00 && src[i + 1] == 0x00 && src[i + 2] == 0x03) {
			dst[di++] = 0x00;
			dst[di++] = 0x00;
			i += 3; /* skip the 0x03 byte */
		} else {
			dst[di++] = src[i++];
		}
	}
	return di;
}

/* ------------------------------------------------------------------ */
/* Parse a single H.264 SEI NAL unit for ATSC closed-caption data      */
/* (ITU-T A/53 Part 4, payload type 4 = registered user data)          */
/*                                                                      */
/* cc_out must have room for at least 93 bytes (31 triplets max).       */
/* Returns cc_count (number of 3-byte triplets written), or 0.          */
/* ------------------------------------------------------------------ */
static int parse_h264_sei_for_cc(const uint8_t *nal, int nal_len, uint8_t *cc_out)
{
	/* Remove emulation prevention bytes first */
	uint8_t *clean = malloc(nal_len);
	if (!clean)
		return 0;
	int clean_len = remove_epb(nal, nal_len, clean);

	int pos = 0;
	int cc_count = 0;

	/* Skip the NAL header byte (type 6 = SEI) */
	if (clean_len < 2) {
		free(clean);
		return 0;
	}
	pos = 1;

	/* Parse SEI messages */
	while (pos < clean_len - 1) {
		/* Read payload type (variable length) */
		int payload_type = 0;
		while (pos < clean_len && clean[pos] == 0xFF) {
			payload_type += 255;
			pos++;
		}
		if (pos >= clean_len)
			break;
		payload_type += clean[pos++];

		/* Read payload size (variable length) */
		int payload_size = 0;
		while (pos < clean_len && clean[pos] == 0xFF) {
			payload_size += 255;
			pos++;
		}
		if (pos >= clean_len)
			break;
		payload_size += clean[pos++];

		if (pos + payload_size > clean_len)
			break;

		/* payload_type 4 = user_data_registered_itu_t_t35 */
		if (payload_type == 4 && payload_size >= 10) {
			const uint8_t *p = clean + pos;
			int remaining = payload_size;

			/* Country code: 0xB5 (United States) */
			if (p[0] != 0xB5)
				goto next_sei;

			/* Provider code: 0x0031 (ATSC) */
			uint16_t provider = (p[1] << 8) | p[2];
			if (provider != 0x0031)
				goto next_sei;

			/* GA94 identifier: "GA94" = 0x47413934 */
			if (remaining < 7)
				goto next_sei;
			if (p[3] != 0x47 || p[4] != 0x41 || p[5] != 0x39 || p[6] != 0x34)
				goto next_sei;

			/* user_data_type_code == 0x03 (cc_data) */
			if (remaining < 9)
				goto next_sei;
			if (p[7] != 0x03)
				goto next_sei;

			/* p[8]: process_em_data_flag(1) | process_cc_data_flag(1) |
			 *       additional_data_flag(1) | cc_count(5) */
			int process_cc_data_flag = (p[8] >> 6) & 1;
			int count = p[8] & 0x1F;

			if (!process_cc_data_flag || count == 0)
				goto next_sei;

			/* p[9] = em_data (skip) */
			/* cc_data starts at p[10], each triplet is 3 bytes */
			if (remaining < 10 + count * 3)
				goto next_sei;

			memcpy(cc_out, p + 10, count * 3);
			cc_count = count;
			break;
		}

next_sei:
		pos += payload_size;
	}

	free(clean);
	return cc_count;
}

/* ------------------------------------------------------------------ */
/* Bit-reading helpers for NAL unit parsing                             */
/* ------------------------------------------------------------------ */
static int read_bits(const uint8_t *data, int total_bits, int *bit_offset, int n)
{
	if (*bit_offset + n > total_bits || n > 24)
		return -1;
	unsigned val = 0;
	for (int i = 0; i < n; i++) {
		int byte_idx = *bit_offset / 8;
		int bit_idx = 7 - (*bit_offset % 8);
		val = (val << 1) | ((data[byte_idx] >> bit_idx) & 1);
		(*bit_offset)++;
	}
	return (int)val;
}

static void skip_bits(int *bit_offset, int n) { *bit_offset += n; }

/* ------------------------------------------------------------------ */
/* Read an unsigned exp-Golomb coded value from a bitstream.            */
/* Returns value on success, or -1 on error / insufficient bits.        */
/* ------------------------------------------------------------------ */
static int read_exp_golomb(const uint8_t *data, int total_bits, int *bit_offset)
{
	int leading_zeros = 0;
	while (*bit_offset < total_bits) {
		int byte_idx = *bit_offset / 8;
		int bit_idx = 7 - (*bit_offset % 8);
		if ((data[byte_idx] >> bit_idx) & 1)
			break;
		leading_zeros++;
		(*bit_offset)++;
		if (leading_zeros > 20) /* sanity limit */
			return -1;
	}
	if (*bit_offset >= total_bits)
		return -1;
	(*bit_offset)++; /* skip the leading 1 bit */

	unsigned value = 0;
	for (int i = 0; i < leading_zeros; i++) {
		if (*bit_offset >= total_bits)
			return -1;
		int byte_idx = *bit_offset / 8;
		int bit_idx = 7 - (*bit_offset % 8);
		value = (value << 1) | ((data[byte_idx] >> bit_idx) & 1);
		(*bit_offset)++;
	}
	return (int)((1u << leading_zeros) - 1 + value);
}

/* ------------------------------------------------------------------ */
/* Skip H.264 HRD parameters in VUI (needed to reach                    */
/* bitstream_restriction_flag).                                         */
/* Returns 0 on success, -1 on error.                                   */
/* ------------------------------------------------------------------ */
static int skip_hrd_parameters(const uint8_t *data, int total_bits, int *bo)
{
	int cpb_cnt_minus1 = read_exp_golomb(data, total_bits, bo);
	if (cpb_cnt_minus1 < 0) return -1;
	skip_bits(bo, 4 + 4); /* bit_rate_scale, cpb_size_scale */
	for (int i = 0; i <= cpb_cnt_minus1; i++) {
		if (read_exp_golomb(data, total_bits, bo) < 0) return -1; /* bit_rate_value_minus1 */
		if (read_exp_golomb(data, total_bits, bo) < 0) return -1; /* cpb_size_value_minus1 */
		skip_bits(bo, 1); /* cbr_flag */
	}
	skip_bits(bo, 5 + 5 + 5 + 5); /* delay lengths + time_offset_length */
	return (*bo <= total_bits) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Skip H.264 scaling list (4x4 or 8x8) in SPS.                        */
/* ------------------------------------------------------------------ */
static int skip_scaling_list(const uint8_t *data, int total_bits, int *bo, int size)
{
	int last_scale = 8, next_scale = 8;
	for (int j = 0; j < size; j++) {
		if (next_scale != 0) {
			/* delta_scale is signed exp-golomb */
			int code = read_exp_golomb(data, total_bits, bo);
			if (code < 0) return -1;
			int delta = (code & 1) ? (code + 1) / 2 : -(code / 2);
			next_scale = (last_scale + delta + 256) % 256;
		}
		last_scale = (next_scale == 0) ? last_scale : next_scale;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Parse H.264 SPS NAL to extract max_num_reorder_frames.               */
/*                                                                      */
/* Priority:                                                            */
/*  1. VUI bitstream_restriction → max_num_reorder_frames (exact)       */
/*  2. Baseline/Constrained Baseline profile → 0 (no B-frames)         */
/*  3. max_num_ref_frames heuristic: 1→1, 2→2, 3-4→4                   */
/*                                                                      */
/* Returns >= 0 on success, -1 only on parse error.                     */
/* ------------------------------------------------------------------ */
static int parse_sps_max_reorder_frames(const uint8_t *nal_data, int nal_len)
{
	uint8_t *clean = malloc(nal_len);
	if (!clean) return -1;
	int clen = remove_epb(nal_data, nal_len, clean);
	int bo = 0, tb = clen * 8;

	/* NAL header (1 byte) */
	skip_bits(&bo, 8);

	/* profile_idc(8) + constraint_set_flags(8) + level_idc(8) */
	int profile_idc = read_bits(clean, tb, &bo, 8);
	if (profile_idc < 0) goto fail;
	int constraint_flags = read_bits(clean, tb, &bo, 8);
	if (constraint_flags < 0) goto fail;
	skip_bits(&bo, 8); /* level_idc */

	/* seq_parameter_set_id */
	if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;

	/* High-profile extensions */
	if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
	    profile_idc == 244 || profile_idc == 44  || profile_idc == 83  ||
	    profile_idc == 86  || profile_idc == 118 || profile_idc == 128 ||
	    profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
	    profile_idc == 135)
	{
		int chroma_format_idc = read_exp_golomb(clean, tb, &bo);
		if (chroma_format_idc < 0) goto fail;
		if (chroma_format_idc == 3)
			skip_bits(&bo, 1); /* separate_colour_plane_flag */
		if (read_exp_golomb(clean, tb, &bo) < 0) goto fail; /* bit_depth_luma_minus8 */
		if (read_exp_golomb(clean, tb, &bo) < 0) goto fail; /* bit_depth_chroma_minus8 */
		skip_bits(&bo, 1); /* qpprime_y_zero_transform_bypass_flag */
		int seq_scaling_matrix_present = read_bits(clean, tb, &bo, 1);
		if (seq_scaling_matrix_present < 0) goto fail;
		if (seq_scaling_matrix_present) {
			int n = (chroma_format_idc != 3) ? 8 : 12;
			for (int i = 0; i < n; i++) {
				int present = read_bits(clean, tb, &bo, 1);
				if (present < 0) goto fail;
				if (present) {
					if (skip_scaling_list(clean, tb, &bo, i < 6 ? 16 : 64) < 0)
						goto fail;
				}
			}
		}
	}

	/* log2_max_frame_num_minus4 */
	if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;

	/* pic_order_cnt_type */
	int poc_type = read_exp_golomb(clean, tb, &bo);
	if (poc_type < 0) goto fail;
	if (poc_type == 0) {
		if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
	} else if (poc_type == 1) {
		skip_bits(&bo, 1);
		if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
		if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
		int num_ref = read_exp_golomb(clean, tb, &bo);
		if (num_ref < 0) goto fail;
		for (int i = 0; i < num_ref; i++) {
			if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
		}
	}

	/* max_num_ref_frames — needed for heuristic fallback */
	int max_ref_frames = read_exp_golomb(clean, tb, &bo);
	if (max_ref_frames < 0) goto fail;

	/* gaps_in_frame_num_value_allowed_flag */
	skip_bits(&bo, 1);
	/* pic_width_in_mbs_minus1 */
	if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
	/* pic_height_in_map_units_minus1 */
	if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
	/* frame_mbs_only_flag */
	int frame_mbs_only = read_bits(clean, tb, &bo, 1);
	if (frame_mbs_only < 0) goto fail;
	if (!frame_mbs_only)
		skip_bits(&bo, 1); /* mb_adaptive_frame_field_flag */
	/* direct_8x8_inference_flag */
	skip_bits(&bo, 1);
	/* frame_cropping_flag */
	int crop = read_bits(clean, tb, &bo, 1);
	if (crop < 0) goto fail;
	if (crop) {
		for (int i = 0; i < 4; i++) {
			if (read_exp_golomb(clean, tb, &bo) < 0) goto fail;
		}
	}

	/* vui_parameters_present_flag */
	int vui_present = read_bits(clean, tb, &bo, 1);
	if (vui_present < 0) goto fail;
	if (!vui_present) goto heuristic;

	/* --- VUI parameters --- */
	int ar_present = read_bits(clean, tb, &bo, 1);
	if (ar_present < 0) goto heuristic;
	if (ar_present) {
		int ar_idc = read_bits(clean, tb, &bo, 8);
		if (ar_idc < 0) goto heuristic;
		if (ar_idc == 255)
			skip_bits(&bo, 16 + 16);
	}
	int overscan_present = read_bits(clean, tb, &bo, 1);
	if (overscan_present < 0) goto heuristic;
	if (overscan_present) skip_bits(&bo, 1);

	int video_signal_present = read_bits(clean, tb, &bo, 1);
	if (video_signal_present < 0) goto heuristic;
	if (video_signal_present) {
		skip_bits(&bo, 3 + 1);
		int colour_desc = read_bits(clean, tb, &bo, 1);
		if (colour_desc < 0) goto heuristic;
		if (colour_desc) skip_bits(&bo, 8 + 8 + 8);
	}
	int chroma_loc_present = read_bits(clean, tb, &bo, 1);
	if (chroma_loc_present < 0) goto heuristic;
	if (chroma_loc_present) {
		if (read_exp_golomb(clean, tb, &bo) < 0) goto heuristic;
		if (read_exp_golomb(clean, tb, &bo) < 0) goto heuristic;
	}
	int timing_present = read_bits(clean, tb, &bo, 1);
	if (timing_present < 0) goto heuristic;
	if (timing_present)
		skip_bits(&bo, 32 + 32 + 1);

	int nal_hrd = read_bits(clean, tb, &bo, 1);
	if (nal_hrd < 0) goto heuristic;
	if (nal_hrd) {
		if (skip_hrd_parameters(clean, tb, &bo) < 0) goto heuristic;
	}
	int vcl_hrd = read_bits(clean, tb, &bo, 1);
	if (vcl_hrd < 0) goto heuristic;
	if (vcl_hrd) {
		if (skip_hrd_parameters(clean, tb, &bo) < 0) goto heuristic;
	}
	if (nal_hrd || vcl_hrd)
		skip_bits(&bo, 1);
	skip_bits(&bo, 1); /* pic_struct_present_flag */

	int bitstream_restriction = read_bits(clean, tb, &bo, 1);
	if (bitstream_restriction < 0) goto heuristic;
	if (!bitstream_restriction) goto heuristic;

	skip_bits(&bo, 1); /* motion_vectors_over_pic_boundaries_flag */
	if (read_exp_golomb(clean, tb, &bo) < 0) goto heuristic;
	if (read_exp_golomb(clean, tb, &bo) < 0) goto heuristic;
	if (read_exp_golomb(clean, tb, &bo) < 0) goto heuristic;
	if (read_exp_golomb(clean, tb, &bo) < 0) goto heuristic;

	int max_reorder = read_exp_golomb(clean, tb, &bo);
	if (max_reorder >= 0) {
		free(clean);
		return max_reorder;
	}

heuristic:
	free(clean);

	/* Baseline (66) and Constrained Baseline (66 + constraint_set1) don't
	 * support B-frames at all. */
	if (profile_idc == 66)
		return 0;

	/* Heuristic from max_num_ref_frames: the reorder distance is at most
	 * max_ref_frames - 1 (one ref is always the previous I/P frame). */
	if (max_ref_frames <= 1) return 1;
	if (max_ref_frames <= 2) return 2;
	return 4;

fail:
	free(clean);
	return -1;
}

/* ------------------------------------------------------------------ */
/* Auto-detect AVCC nal_length_size from first packet data.             */
/* Tries 4, 2, 1 in order; validates with length + NAL type checks.    */
/* Returns detected size, or 4 as fallback.                             */
/* ------------------------------------------------------------------ */
static int auto_detect_avcc_nal_size(const uint8_t *data, int size)
{
	int candidates[] = {4, 2, 1};
	for (int c = 0; c < 3; c++) {
		int nls = candidates[c];
		if (nls > size)
			continue;

		uint32_t nal_len = 0;
		for (int i = 0; i < nls; i++)
			nal_len = (nal_len << 8) | data[i];

		if (nal_len == 0 || nls + (int)nal_len > size)
			continue;

		/* Validate NAL type byte: forbidden_zero_bit must be 0, type non-zero */
		uint8_t first_byte = data[nls];
		if (first_byte & 0x80)
			continue;
		uint8_t nal_type = first_byte & 0x1F;
		if (nal_type == 0)
			continue;

		return nls;
	}
	return 4; /* fallback */
}

/* ------------------------------------------------------------------ */
/* Public entry point: extract cc_data from H.264 packet.               */
/* ------------------------------------------------------------------ */
cea_demux_result cea_demux_h264_extract_cc(int is_avcc, int *nal_length_size,
                                           const uint8_t *data, int size,
                                           uint8_t *cc_out)
{
	cea_demux_result result = {0, -1};
	int sps_result = -1;

	/* Auto-detect nal_length_size for AVCC on first call */
	if (is_avcc && *nal_length_size == 0)
		*nal_length_size = auto_detect_avcc_nal_size(data, size);

	if (is_avcc) {
		/* AVCC format: length-prefixed NAL units */
		int nls = *nal_length_size;
		int pos = 0;
		while (pos + nls <= size) {
			uint32_t nal_len = 0;
			for (int i = 0; i < nls; i++)
				nal_len = (nal_len << 8) | data[pos + i];
			pos += nls;

			if (nal_len == 0 || pos + (int)nal_len > size)
				break;

			uint8_t nal_type = data[pos] & 0x1F;

			if (nal_type == 7 && sps_result < 0) {
				int mr = parse_sps_max_reorder_frames(data + pos, (int)nal_len);
				if (mr >= 0) sps_result = mr;
			}
			if (nal_type == 6 && result.cc_count == 0) {
				result.cc_count = parse_h264_sei_for_cc(data + pos, (int)nal_len, cc_out);
			}

			pos += (int)nal_len;
		}
	} else {
		/* Annex B: scan for start codes (00 00 01 or 00 00 00 01) */
		int pos = 0;
		while (pos + 3 < size) {
			int sc_len = 0;
			if (data[pos] == 0x00 && data[pos + 1] == 0x00) {
				if (data[pos + 2] == 0x01) {
					sc_len = 3;
				} else if (pos + 3 < size && data[pos + 2] == 0x00 && data[pos + 3] == 0x01) {
					sc_len = 4;
				}
			}

			if (sc_len == 0) {
				pos++;
				continue;
			}

			int nal_start = pos + sc_len;
			if (nal_start >= size)
				break;
			uint8_t nal_type = data[nal_start] & 0x1F;

			/* Find end of this NAL (next start code or end of data) */
			int nal_end = size;
			for (int j = nal_start + 1; j + 2 < size; j++) {
				if (data[j] == 0x00 && data[j + 1] == 0x00 &&
				    (data[j + 2] == 0x01 || (j + 3 < size && data[j + 2] == 0x00 && data[j + 3] == 0x01))) {
					nal_end = j;
					break;
				}
			}

			int nal_len = nal_end - nal_start;

			if (nal_type == 7 && sps_result < 0) {
				int mr = parse_sps_max_reorder_frames(data + nal_start, nal_len);
				if (mr >= 0) sps_result = mr;
			}
			if (nal_type == 6 && result.cc_count == 0) {
				result.cc_count = parse_h264_sei_for_cc(data + nal_start, nal_len, cc_out);
			}

			pos = nal_end;
		}
	}

	result.reorder_window = sps_result;
	return result;
}

/* ------------------------------------------------------------------ */
/* Parse H.264 extradata for max_num_reorder_frames.                    */
/* Handles both Annex B (start-code delimited) and AVCC (length-prefix) */
/* formats. Returns >= 0 on success, -1 on failure.                     */
/* ------------------------------------------------------------------ */
int cea_demux_h264_parse_extradata_reorder(const uint8_t *extradata, int size)
{
	if (!extradata || size < 4)
		return -1;

	/* AVCC format: starts with configurationVersion == 1 */
	if (extradata[0] == 1 && size >= 8) {
		/* AVCC header: version(1) + profile(1) + compat(1) + level(1)
		 * + lengthSizeMinusOne(1) + numOfSPS(1) + [spsLength(2) + spsNAL]... */
		int num_sps = extradata[5] & 0x1F;
		int pos = 6;
		for (int i = 0; i < num_sps; i++) {
			if (pos + 2 > size) return -1;
			int sps_len = (extradata[pos] << 8) | extradata[pos + 1];
			pos += 2;
			if (pos + sps_len > size) return -1;
			int mr = parse_sps_max_reorder_frames(extradata + pos, sps_len);
			if (mr >= 0) return mr;
			pos += sps_len;
		}
		return -1;
	}

	/* Annex B format: scan for start codes, find NAL type 7 (SPS) */
	int pos = 0;
	while (pos + 3 < size) {
		int sc_len = 0;
		if (extradata[pos] == 0x00 && extradata[pos + 1] == 0x00) {
			if (extradata[pos + 2] == 0x01)
				sc_len = 3;
			else if (pos + 3 < size && extradata[pos + 2] == 0x00 && extradata[pos + 3] == 0x01)
				sc_len = 4;
		}
		if (sc_len == 0) { pos++; continue; }

		int nal_start = pos + sc_len;
		if (nal_start >= size) break;
		uint8_t nal_type = extradata[nal_start] & 0x1F;

		/* Find end of this NAL */
		int nal_end = size;
		for (int j = nal_start + 1; j + 2 < size; j++) {
			if (extradata[j] == 0x00 && extradata[j + 1] == 0x00 &&
			    (extradata[j + 2] == 0x01 ||
			     (j + 3 < size && extradata[j + 2] == 0x00 && extradata[j + 3] == 0x01))) {
				nal_end = j;
				break;
			}
		}

		if (nal_type == 7) {
			int mr = parse_sps_max_reorder_frames(extradata + nal_start, nal_end - nal_start);
			if (mr >= 0) return mr;
		}
		pos = nal_end;
	}
	return -1;
}
