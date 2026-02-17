/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#ifndef _CEA_COMMON_COMMON
#define _CEA_COMMON_COMMON

#include "cea_common_structs.h"
#include "cea.h"

#include <stdlib.h>

/* Exit codes */
#define EXIT_OK 0
#define EXIT_NO_INPUT_FILES 2
#define EXIT_TOO_MANY_INPUT_FILES 3
#define EXIT_INCOMPATIBLE_PARAMETERS 4
#define EXIT_UNABLE_TO_DETERMINE_FILE_SIZE 6
#define EXIT_MALFORMED_PARAMETER 7
#define EXIT_READ_ERROR 8
#define EXIT_NO_CAPTIONS 10
#define EXIT_WITH_HELP 11
#define EXIT_NOT_CLASSIFIED 300
#define EXIT_ERROR_IN_CAPITALIZATION_FILE 501
#define EXIT_BUFFER_FULL 502
#define EXIT_MISSING_ASF_HEADER 1001

#define CEA_COMMON_EXIT_FILE_CREATION_FAILED 5
#define CEA_COMMON_EXIT_UNSUPPORTED 9
#define EXIT_NOT_ENOUGH_MEMORY 500
#define CEA_COMMON_EXIT_BUG_BUG 1000

/* Single logging function -- all logging goes through here */
void cea_log(cea_log_level level, const char *fmt, ...);

/* Active debug mask (synced from context on each feed/flush entry) */
extern int64_t cea_log_debug_mask;

/* Activate the logger state for the current context */
void cea_log_activate(cea_log_callback cb, void *ud, cea_log_level min_level, int64_t debug_mask);

/* Convenience wrappers */
#define mprint(...) cea_log(CEA_LOG_INFO, __VA_ARGS__)
#define dbg_print(mask, ...) do { if (cea_log_debug_mask & (mask)) cea_log(CEA_LOG_DEBUG, __VA_ARGS__); } while (0)
#define fatal(code, ...) do { cea_log(CEA_LOG_FATAL, __VA_ARGS__); exit(code); } while (0)

/* Declarations */
int cc608_parity(unsigned int byte);
void millis_to_time(int64_t milli, unsigned *hours, unsigned *minutes, unsigned *seconds, unsigned *ms);

void freep(void *arg);
unsigned char *debug_608_to_ASC(unsigned char *ccdata, int channel);
int add_cc_sub_text(struct cc_subtitle *sub, char *str, int64_t start_time,
		    int64_t end_time, char *info, char *mode);

extern int cc608_parity_table[256];
void build_parity_table(void);

#ifndef VERSION
#define VERSION "cea-0.1"
#endif

#endif
