/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_demux.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Scan an MPEG-2 video packet for user_data_start_code (00 00 01 B2)  */
/* containing GA94 cc_data.                                             */
/* Returns cc_count, fills cc_out.                                      */
/* ------------------------------------------------------------------ */
static int parse_mpeg2_userdata_for_cc(const uint8_t *data, int data_len, uint8_t *cc_out)
{
	int cc_count = 0;

	for (int i = 0; i + 3 < data_len; i++) {
		/* Look for user_data_start_code: 00 00 01 B2 */
		if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01 && data[i + 3] == 0xB2) {
			const uint8_t *ud = data + i + 4;
			int ud_len = data_len - i - 4;

			/* Find end of this user data block (next start code or end of data) */
			for (int j = 0; j + 2 < ud_len; j++) {
				if (ud[j] == 0x00 && ud[j + 1] == 0x00 && ud[j + 2] == 0x01) {
					ud_len = j;
					break;
				}
			}

			/* Need at least: GA94(4) + type(1) + flags(1) + em(1) = 7 bytes */
			if (ud_len < 7)
				continue;

			/* Check GA94 identifier */
			if (ud[0] != 0x47 || ud[1] != 0x41 || ud[2] != 0x39 || ud[3] != 0x34)
				continue;

			/* user_data_type_code == 0x03 */
			if (ud[4] != 0x03)
				continue;

			int process_cc_data_flag = (ud[5] >> 6) & 1;
			int count = ud[5] & 0x1F;

			if (!process_cc_data_flag || count == 0)
				continue;

			/* ud[6] = em_data (skip), cc_data starts at ud[7] */
			if (ud_len < 7 + count * 3)
				continue;

			memcpy(cc_out, ud + 7, count * 3);
			cc_count = count;
			break;
		}
	}

	return cc_count;
}

/* ------------------------------------------------------------------ */
/* Public entry point: extract cc_data from MPEG-2 packet.              */
/* ------------------------------------------------------------------ */
cea_demux_result cea_demux_mpeg2_extract_cc(const uint8_t *data, int size,
                                            uint8_t *cc_out)
{
	/* Detect picture_coding_type from the picture_start_code (00 00 01 00).
	 * MPEG-2 packets arrive in decode (DTS) order from the container.  B-frames
	 * have a lower display PTS than the P-frame decoded before them, so a reorder
	 * buffer is needed.  Signal reorder_window=2 when a B-frame is found; leave
	 * the window at -1 (no update) for I/P frames so the caller keeps whatever
	 * window it determined from earlier in the stream.
	 */
	cea_demux_result result = {0, -1};
	for (int i = 0; i + 5 < size; i++) {
		if (data[i] == 0x00 && data[i+1] == 0x00 &&
		    data[i+2] == 0x01 && data[i+3] == 0x00) {
			/* picture_coding_type is bits [5:3] of the byte at offset +5 */
			int pct = (data[i+5] >> 3) & 0x07;
			if (pct == 3) /* B-frame: needs reorder buffer */
				result.reorder_window = 2;
			break;
		}
	}
	result.cc_count = parse_mpeg2_userdata_for_cc(data, size, cc_out);
	return result;
}
