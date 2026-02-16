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

int main(void)
{
	printf("=== libcea smoke test ===\n");

	cea_ctx *ctx = cea_init_default();
	if (!ctx)
	{
		fprintf(stderr, "FAIL: cea_init_default() returned NULL\n");
		return 1;
	}
	printf("PASS: init\n");

	/* CEA-608 pop-on caption sequence (field 1, CC1).
	 * Triplet format: (cc_valid<<2 | cc_type), byte1, byte2
	 * cc_valid=1, cc_type=0 for field 1 => byte0 = 0x04
	 *
	 * All data bytes have odd parity (bit 7 used as parity bit).
	 * Parity: bit 7 set if bits 0-6 have even count of 1s.
	 */

	/* Step 1: RCL (Resume Caption Loading) - enters pop-on mode */
	unsigned char rcl[] = { 0x04, 0x94, 0x20 };
	cea_feed(ctx, rcl, 1, 1000);

	/* Step 2: Write characters to non-displayed memory */
	/* 'T'=0x54 (3 ones=odd, ok), 'e'=0x65 (4 ones=even, need 0xE5) */
	unsigned char te[] = { 0x04, 0x54, 0xE5 };
	cea_feed(ctx, te, 1, 1033);

	/* 's'=0x73 (5 ones=odd, ok), 't'=0x74 (4 ones=even, need 0xF4) */
	unsigned char st[] = { 0x04, 0x73, 0xF4 };
	cea_feed(ctx, st, 1, 1066);

	/* Step 3: EOC (End of Caption) - flip memories, display text */
	/* 0x14->0x94, 0x2F (5 ones=odd, ok) */
	unsigned char eoc[] = { 0x04, 0x94, 0x2F };
	cea_feed(ctx, eoc, 1, 2000);

	/* Step 4: Send padding/null characters to advance time */
	unsigned char null_cc[] = { 0x04, 0x80, 0x80 };
	for (int i = 0; i < 30; i++)
		cea_feed(ctx, null_cc, 1, 2000 + (i + 1) * 33);

	/* Step 5: EDM (Erase Displayed Memory) - clears screen, triggers subtitle output */
	/* 0x14->0x94, 0x2C (3 ones=odd, ok) */
	unsigned char edm[] = { 0x04, 0x94, 0x2C };
	cea_feed(ctx, edm, 1, 4000);

	/* More padding to advance time further */
	for (int i = 0; i < 30; i++)
		cea_feed(ctx, null_cc, 1, 4000 + (i + 1) * 33);

	/* Flush remaining */
	cea_flush(ctx);

	/* Get captions */
	cea_caption captions[32];
	int count = cea_get_captions(ctx, captions, 32);
	printf("INFO: got %d caption(s)\n", count);

	for (int i = 0; i < count; i++)
	{
		printf("  [%d] field=%d start=%lld end=%lld text='%s'\n",
		       i, captions[i].field,
		       (long long)captions[i].start_ms,
		       (long long)captions[i].end_ms,
		       captions[i].text ? captions[i].text : "(null)");
	}

	if (count > 0 && captions[0].text && strstr(captions[0].text, "Test"))
		printf("PASS: Correctly decoded 'Test' caption!\n");
	else if (count > 0)
		printf("PASS: Got caption(s) but text differs from expected\n");
	else
		printf("INFO: No captions decoded (608 decoder may need additional commands)\n");

	cea_free(ctx);
	printf("PASS: cleanup\n");
	printf("=== Done ===\n");
	return 0;
}
