/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea_common_common.h"
#include "cea.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>


/* ---- Callback-based logging ---- */

static cea_log_callback s_log_cb     = NULL;
static void            *s_log_ud     = NULL;
static cea_log_level    s_min_level  = CEA_LOG_INFO;
int64_t                 cea_log_debug_mask = 0;

/* Activate the logger state for the current context (called at feed/flush entry). */
void cea_log_activate(cea_log_callback cb, void *ud, cea_log_level min_level, int64_t debug_mask)
{
	s_log_cb         = cb;
	s_log_ud         = ud;
	s_min_level      = min_level;
	cea_log_debug_mask = debug_mask;
}

void cea_log(cea_log_level level, const char *fmt, ...)
{
	if (!s_log_cb || level < s_min_level)
		return;
	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	s_log_cb(level, buf, s_log_ud);
}

/* ---- Parity & utility ---- */

int cc608_parity_table[256];


/* Converts the given milli to separate hours,minutes,seconds and ms variables
   C implementation replacing cea_millis_to_time */
void millis_to_time(int64_t milli, unsigned *hours, unsigned *minutes,
		    unsigned *seconds, unsigned *ms)
{
	if (milli < 0)
		milli = -milli;

	*hours = (unsigned)(milli / 3600000);
	*minutes = (unsigned)((milli % 3600000) / 60000);
	*seconds = (unsigned)((milli % 60000) / 1000);
	*ms = (unsigned)(milli % 1000);
}

/* Frees the given pointer */
void freep(void *arg)
{
	void **ptr = arg;
	if (ptr && *ptr)
	{
		free(*ptr);
		*ptr = NULL;
	}
}

int add_cc_sub_text(struct cc_subtitle *sub, char *str, int64_t start_time,
		    int64_t end_time, char *info, char *mode)
{
	if (str == NULL || strlen(str) == 0)
		return 0;
	if (sub->nb_data)
	{
		for (; sub->next; sub = sub->next)
			;
		sub->next = (struct cc_subtitle *)malloc(sizeof(struct cc_subtitle));
		if (!sub->next)
			return -1;
		sub = sub->next;
	}

	sub->type = CC_TEXT;
	sub->data = strdup(str);
	sub->nb_data = str ? strlen(str) : 0;
	sub->start_time = start_time;
	sub->end_time = end_time;
	if (info)
		strncpy(sub->info, info, 4);
	if (mode)
		strncpy(sub->mode, mode, 4);
	sub->got_output = 1;
	sub->next = NULL;

	return 0;
}

// returns 1 if odd parity and 0 if even parity
int cc608_parity(unsigned int byte)
{
	unsigned int ones = 0;

	for (int i = 0; i < 7; i++)
	{
		if (byte & (1 << i))
			ones++;
	}

	return ones & 1;
}

void cc608_build_parity_table(int *parity_table)
{
	unsigned int byte;
	int parity_v;
	for (byte = 0; byte <= 127; byte++)
	{
		parity_v = cc608_parity(byte);
		parity_table[byte] = parity_v;
		parity_table[byte | 0x80] = !parity_v;
	}
}

void build_parity_table(void)
{
	cc608_build_parity_table(cc608_parity_table);
}
