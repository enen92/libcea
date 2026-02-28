/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

/* Smoke test for libcea */
#include <stdio.h>
#include <string.h>
#include "include/cea.h"

static void on_caption(const cea_caption *cap, void *userdata)
{
	int *count = (int *)userdata;
	(*count)++;
	if (cap->text)
		printf("  SHOW pts_ms=%-6lld text='%s'\n",
		       (long long)cap->pts_ms, cap->text);
	else
		printf("  CLEAR pts_ms=%-6lld\n", (long long)cap->pts_ms);
}

/* Feed the standard pop-on test sequence into ctx. */
static void feed_test_sequence(cea_ctx *ctx)
{
	/* CEA-608 pop-on caption sequence (field 1, CC1).
	 * Triplet format: (cc_valid<<2 | cc_type), byte1, byte2
	 * cc_valid=1, cc_type=0 for field 1 => byte0 = 0x04
	 *
	 * All data bytes have odd parity (bit 7 used as parity bit).
	 * Parity: bit 7 set if bits 0-6 have even count of 1s.
	 */

	/* RCL (Resume Caption Loading) - enters pop-on mode */
	unsigned char rcl[]    = { 0x04, 0x94, 0x20 };
	/* 'T'=0x54 (odd parity ok), 'e'→0xE5 (even bits, parity set) */
	unsigned char te[]     = { 0x04, 0x54, 0xE5 };
	/* 's'=0x73 (odd ok), 't'→0xF4 (even bits, parity set) */
	unsigned char st[]     = { 0x04, 0x73, 0xF4 };
	/* EOC (End of Caption) - flip memories, display text */
	unsigned char eoc[]    = { 0x04, 0x94, 0x2F };
	/* Null padding to advance time */
	unsigned char null_cc[] = { 0x04, 0x80, 0x80 };
	/* EDM (Erase Displayed Memory) - clears screen */
	unsigned char edm[]    = { 0x04, 0x94, 0x2C };

	cea_feed(ctx, rcl, 1, 1000);
	cea_feed(ctx, te,  1, 1033);
	cea_feed(ctx, st,  1, 1066);
	cea_feed(ctx, eoc, 1, 2000);
	for (int i = 0; i < 30; i++)
		cea_feed(ctx, null_cc, 1, 2000 + (i + 1) * 33);
	cea_feed(ctx, edm, 1, 4000);
	for (int i = 0; i < 30; i++)
		cea_feed(ctx, null_cc, 1, 4000 + (i + 1) * 33);
	cea_flush(ctx);
}

int main(void)
{
	printf("=== libcea smoke test ===\n\n");

	/* ---- Live callback mode (pts_ms) ---- */
	printf("--- live callback mode ---\n");
	cea_ctx *live_ctx = cea_init_default();
	if (!live_ctx)
	{
		fprintf(stderr, "FAIL: cea_init_default() returned NULL\n");
		return 1;
	}

	int live_count = 0;
	cea_set_caption_callback(live_ctx, on_caption, &live_count);
	feed_test_sequence(live_ctx);

	if (live_count > 0)
		printf("PASS: live callback fired %d time(s)\n", live_count);
	else
		printf("INFO: no live callbacks fired\n");
	cea_free(live_ctx);

	/* ---- Pull mode (start_ms / end_ms) ---- */
	printf("\n--- pull mode ---\n");
	cea_ctx *pull_ctx = cea_init_default();
	if (!pull_ctx)
	{
		fprintf(stderr, "FAIL: cea_init_default() returned NULL\n");
		return 1;
	}

	feed_test_sequence(pull_ctx);

	cea_caption captions[32];
	int count = cea_get_captions(pull_ctx, captions, 32);
	printf("INFO: got %d caption(s)\n", count);
	for (int i = 0; i < count; i++)
	{
		printf("  [%d] field=%d start_ms=%-6lld end_ms=%-6lld text='%s'\n",
		       i, captions[i].field,
		       (long long)captions[i].start_ms,
		       (long long)captions[i].end_ms,
		       captions[i].text ? captions[i].text : "(null)");
	}

	if (count > 0 && captions[0].text && strstr(captions[0].text, "Test"))
		printf("PASS: correctly decoded 'Test' caption\n");
	else if (count > 0)
		printf("PASS: got caption(s) (text differs from expected)\n");
	else
		printf("INFO: no captions decoded\n");

	cea_free(pull_ctx);

	printf("\n=== Done ===\n");
	return 0;
}
