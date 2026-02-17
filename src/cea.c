/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

#include "cea.h"

#include "cea_decoders_common.h"
#include "cea_common_timing.h"
#include "cea_common_common.h"
#include "cea_common_char_encoding.h"
#include "cea_decoders_608.h"
#include "cea_decoders_708.h"
#include "cea_demux.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *cea_version(void)
{
	return CEA_VERSION_STRING;
}

/* Map cc_modes enum to short mode string */
static const char *mode_str(enum cc_modes mode)
{
	switch (mode)
	{
		case MODE_POPON:        return "POP";
		case MODE_ROLLUP_2:     return "RU2";
		case MODE_ROLLUP_3:     return "RU3";
		case MODE_ROLLUP_4:     return "RU4";
		case MODE_TEXT:         return "TXT";
		case MODE_PAINTON:      return "PAI";
		case MODE_FAKE_ROLLUP_1: return "RU1";
		default:                return "";
	}
}

/* Map 608 color enum to HTML hex string. Returns NULL for default/transparent. */
static const char *color_608_hex(enum cea_decoder_608_color_code c)
{
	switch (c)
	{
		case COL_WHITE:       return NULL; /* default, no tag needed */
		case COL_GREEN:       return "#00FF00";
		case COL_BLUE:        return "#0000FF";
		case COL_CYAN:        return "#00FFFF";
		case COL_RED:         return "#FF0000";
		case COL_YELLOW:      return "#FFFF00";
		case COL_MAGENTA:     return "#FF00FF";
		case COL_BLACK:       return "#000000";
		default:              return NULL;
	}
}

/*
 * Build SRT-styled UTF-8 text from a 608 screen.
 * Emits <i>, <u>, <font color="..."> tags around styled runs.
 * Returns malloc'd string (caller must free), or NULL if screen is empty.
 * Sets *out_bottom_row to the last row index with content (-1 if none).
 */
static char *screen_608_to_styled_text(struct eia608_screen *screen, int *out_bottom_row)
{
	/* Worst case: 15 rows, 32 chars each expanding to ~60 bytes with tags */
	size_t cap = 15 * 32 * 60 + 256;
	char *buf = (char *)malloc(cap);
	if (!buf)
		return NULL;
	size_t len = 0;
	int rows_written = 0;
	int bottom_row = -1;

	for (int r = 0; r < 15; r++)
	{
		if (!screen->row_used[r])
			continue;
		bottom_row = r;

		/* Trim trailing spaces */
		int last = 31;
		while (last >= 0 && screen->characters[r][last] == ' ')
			last--;
		if (last < 0)
			continue;

		if (rows_written > 0)
			buf[len++] = '\n';

		/* Track current style state */
		const char *cur_color = NULL; /* NULL = no <font> tag open */
		int cur_italic = 0;
		int cur_underline = 0;

		for (int c = 0; c <= last; c++)
		{
			enum cea_decoder_608_color_code col = screen->colors[r][c];
			enum font_bits font = screen->fonts[r][c];
			const char *hex = color_608_hex(col);
			int want_italic = (font == FONT_ITALICS || font == FONT_UNDERLINED_ITALICS);
			int want_underline = (font == FONT_UNDERLINED || font == FONT_UNDERLINED_ITALICS);

			/* Close tags if style changed (reverse order) */
			if (cur_underline && !want_underline)
			{
				memcpy(buf + len, "</u>", 4); len += 4;
				cur_underline = 0;
			}
			if (cur_italic && !want_italic)
			{
				memcpy(buf + len, "</i>", 4); len += 4;
				cur_italic = 0;
			}
			if (cur_color && cur_color != hex)
			{
				memcpy(buf + len, "</font>", 7); len += 7;
				cur_color = NULL;
			}

			/* Open tags if needed */
			if (hex && cur_color != hex)
			{
				len += sprintf(buf + len, "<font color=\"%s\">", hex);
				cur_color = hex;
			}
			if (want_italic && !cur_italic)
			{
				memcpy(buf + len, "<i>", 3); len += 3;
				cur_italic = 1;
			}
			if (want_underline && !cur_underline)
			{
				memcpy(buf + len, "<u>", 3); len += 3;
				cur_underline = 1;
			}

			/* Write the character as UTF-8 */
			int bytes = get_char_in_utf_8((unsigned char *)buf + len, screen->characters[r][c]);
			len += bytes;
		}

		/* Close any remaining open tags at end of row */
		if (cur_underline)
		{ memcpy(buf + len, "</u>", 4); len += 4; }
		if (cur_italic)
		{ memcpy(buf + len, "</i>", 4); len += 4; }
		if (cur_color)
		{ memcpy(buf + len, "</font>", 7); len += 7; }

		rows_written++;
	}

	buf[len] = '\0';
	if (out_bottom_row)
		*out_bottom_row = bottom_row;

	if (len == 0)
	{
		free(buf);
		return NULL;
	}
	return buf;
}

/* Internal context */
struct cea_ctx
{
	struct lib_cc_decode *dec;
	struct cea_common_timing_ctx *timing;
	struct cc_subtitle sub;     /* 608 subtitle output */
	struct cc_subtitle sub_708; /* 708 subtitle output (separate to avoid corruption) */
	/* Heap-allocated report structs (decoder stores pointers to these) */
	struct cea_decoder_608_report *report_608;
	struct cea_decoder_dtvcc_report *report_708;
	/* Storage for extracted captions */
	cea_caption *captions;
	int caption_count;
	int caption_capacity;
	/* Storage for caption text strings (freed on next call) */
	char **text_storage;
	int text_count;
	/* Demuxer state */
	int demuxer_configured;
	cea_codec_type codec;
	cea_packaging_type packaging;
	int nal_length_size;           /* AVCC only: 0 = not yet detected, then 1-4 */
	int max_reorder_frames;        /* From SPS: -1=unknown, 0+=parsed */
	int reorder_window_override;   /* From user options: 0=auto, >0=override */
	/* Reorder buffer for B-frame PTS sorting */
	struct cc_reorder_entry {
		int64_t pts_ms;
		int cc_count;
		unsigned char cc_data[31 * 3];
	} *reorder_buf;
	int reorder_count;
	int reorder_cap;
	/* Live / streaming callback (optional) */
	cea_caption_callback live_cb;
	void *live_cb_userdata;
	/* last current_visible_start_ms we reported per 608 field (index 0=field1, 1=field2) */
	int64_t live_screen_start_ms[2];
};

/* Free previously stored captions text */
static void clear_caption_storage(cea_ctx *ctx)
{
	for (int i = 0; i < ctx->text_count; i++)
		free(ctx->text_storage[i]);
	free(ctx->text_storage);
	ctx->text_storage = NULL;
	ctx->text_count = 0;
	ctx->caption_count = 0;
}

/* Count captions in a subtitle chain */
static int count_sub_chain(struct cc_subtitle *head)
{
	int count = 0;
	for (struct cc_subtitle *s = head; s; s = s->next)
	{
		if (s->got_output && s->nb_data > 0)
		{
			if (s->type == CC_608)
				count += (int)s->nb_data;
			else
				count++;
		}
	}
	return count;
}

/* Walk the cc_subtitle linked list and extract captions */
static void collect_captions(cea_ctx *ctx)
{
	clear_caption_storage(ctx);

	int count = count_sub_chain(&ctx->sub) + count_sub_chain(&ctx->sub_708);

	if (count == 0)
		return;

	ctx->captions = (cea_caption *)realloc(ctx->captions, count * sizeof(cea_caption));
	ctx->text_storage = (char **)malloc(count * sizeof(char *));
	if (!ctx->captions || !ctx->text_storage)
		return;

	/* Extract from both chains: 608 first, then 708 */
	int idx = 0;
	struct cc_subtitle *chains[2] = { &ctx->sub, &ctx->sub_708 };
	for (int chain = 0; chain < 2; chain++)
	{
		for (struct cc_subtitle *s = chains[chain]; s && idx < count; s = s->next)
		{
			if (!s->got_output || s->nb_data == 0)
				continue;

			if (s->type == CC_TEXT && s->data)
			{
				ctx->text_storage[idx] = strdup((char *)s->data);
				ctx->captions[idx].text = ctx->text_storage[idx];
				ctx->captions[idx].start_ms = s->start_time;
				ctx->captions[idx].end_ms = s->end_time;
				/* Determine field from info */
				if (s->info[0] == '7')
					ctx->captions[idx].field = 3;
				else
					ctx->captions[idx].field = 1;
				ctx->captions[idx].base_row = s->flags; /* set by 708 output */
				strncpy(ctx->captions[idx].mode, s->mode, 4);
				ctx->captions[idx].mode[4] = '\0';
				strncpy(ctx->captions[idx].info, s->info, 3);
				ctx->captions[idx].info[3] = '\0';
				idx++;
			}
			else if (s->type == CC_608 && s->data)
			{
				struct eia608_screen *screens = (struct eia608_screen *)s->data;
				for (unsigned int si = 0; si < s->nb_data && idx < count; si++)
				{
					struct eia608_screen *screen = &screens[si];
					int bottom_row = -1;
					char *text = screen_608_to_styled_text(screen, &bottom_row);
					if (!text)
						continue;
					ctx->text_storage[idx] = text;
					ctx->captions[idx].text = text;
					ctx->captions[idx].start_ms = screen->start_time;
					ctx->captions[idx].end_ms = screen->end_time;
					ctx->captions[idx].field = screen->my_field;
					ctx->captions[idx].base_row = bottom_row;
					strncpy(ctx->captions[idx].mode, mode_str(screen->mode), 4);
					ctx->captions[idx].mode[4] = '\0';
					strncpy(ctx->captions[idx].info, "608", 3);
					ctx->captions[idx].info[3] = '\0';
					idx++;
				}
			}
		}
	}

	ctx->caption_count = idx;
	ctx->text_count = idx;
}

/* Free the linked cc_subtitle chain (except the head which is embedded) */
static void free_sub_chain(struct cc_subtitle *head)
{
	struct cc_subtitle *cur = head->next;
	while (cur)
	{
		struct cc_subtitle *next = cur->next;
		freep(&cur->data);
		free(cur);
		cur = next;
	}
	freep(&head->data);
	memset(head, 0, sizeof(struct cc_subtitle));
}

void cea_set_caption_callback(cea_ctx *ctx, cea_caption_callback cb, void *userdata)
{
	if (!ctx)
		return;
	ctx->live_cb = cb;
	ctx->live_cb_userdata = userdata;
	ctx->live_screen_start_ms[0] = 0;
	ctx->live_screen_start_ms[1] = 0;
}

/*
 * fire_live_callbacks — called at the end of every cea_feed() and cea_flush().
 *
 * Phase 1: drain the completed sub-chains and emit "clear" events (end_ms known).
 *          For CEA-708, also emit a preceding "show" event because there is no
 *          Phase 2 equivalent for the 708 decoder.
 *
 * Phase 2: peek at each 608 decoder's current visible screen buffer.  If the
 *          screen has content and current_visible_start_ms has changed since we
 *          last reported it, emit a "show" event immediately (end_ms still 0).
 */
static void fire_live_callbacks(cea_ctx *ctx)
{
	if (!ctx->live_cb)
		return;

	/* ---- Phase 1: completed captions → end (and 708 start+end) events ---- */
	collect_captions(ctx);

	for (int i = 0; i < ctx->caption_count; i++) {
		cea_caption *cap = &ctx->captions[i];

		if (cap->field == 3) {
			/* CEA-708: no Phase 2, so fire the "show" event here first */
			cea_caption show = *cap;
			show.end_ms = 0;
			ctx->live_cb(&show, ctx->live_cb_userdata);
		}

		/* Fire the "clear" event */
		cea_caption clr = *cap;
		clr.text     = NULL;
		clr.start_ms = 0;
		ctx->live_cb(&clr, ctx->live_cb_userdata);

		/* Reset live tracking for EIA-608 fields */
		if (cap->field == 1 || cap->field == 2)
			ctx->live_screen_start_ms[cap->field - 1] = 0;
	}

	/* Sub-chains consumed; free them so cea_get_captions() returns 0 */
	if (ctx->caption_count > 0) {
		free_sub_chain(&ctx->sub);
		free_sub_chain(&ctx->sub_708);
	}

	/* ---- Phase 2: peek at current EIA-608 visible screen buffers ---- */
	cea_decoder_608_context *ctx608[2] = {
		(cea_decoder_608_context *)ctx->dec->context_cc608_field_1,
		(cea_decoder_608_context *)ctx->dec->context_cc608_field_2,
	};

	for (int f = 0; f < 2; f++) {
		cea_decoder_608_context *c = ctx608[f];
		if (!c)
			continue;

		/* Resolve current visible screen buffer directly */
		struct eia608_screen *vis = (c->visible_buffer == 1) ? &c->buffer1 : &c->buffer2;

		if (vis->empty)
			continue;

		if (c->current_visible_start_ms == ctx->live_screen_start_ms[f])
			continue; /* already reported this screen epoch */

		int bottom_row = -1;
		char *text = screen_608_to_styled_text(vis, &bottom_row);
		if (!text)
			continue;

		cea_caption cap = {0};
		cap.text      = text;
		cap.start_ms  = c->current_visible_start_ms;
		cap.end_ms    = 0;
		cap.field     = f + 1;
		cap.base_row  = bottom_row;
		strncpy(cap.mode, mode_str(c->mode), 4);
		cap.mode[4]   = '\0';
		strncpy(cap.info, "608", 3);
		cap.info[3]   = '\0';

		ctx->live_cb(&cap, ctx->live_cb_userdata);
		ctx->live_screen_start_ms[f] = c->current_visible_start_ms;
		free(text);
	}
}

cea_ctx *cea_init(const cea_options *opts)
{
	/* Build the parity table */
	build_parity_table();

	/* Init timing subsystem */
	static int64_t file_pos = 0;
	cea_common_timing_init(&file_pos, 0);

	cea_ctx *ctx = (cea_ctx *)calloc(1, sizeof(cea_ctx));
	if (!ctx)
		return NULL;

	/* Heap-allocate report structs -- decoders store pointers to these */
	ctx->report_608 = (struct cea_decoder_608_report *)calloc(1, sizeof(struct cea_decoder_608_report));
	ctx->report_708 = (struct cea_decoder_dtvcc_report *)calloc(1, sizeof(struct cea_decoder_dtvcc_report));
	if (!ctx->report_608 || !ctx->report_708)
	{
		free(ctx->report_608);
		free(ctx->report_708);
		free(ctx);
		return NULL;
	}

	/* Set up decoder settings */
	struct cea_decoder_608_settings settings_608 = {0};
	settings_608.report = ctx->report_608;
	settings_608.screens_to_process = -1;
	settings_608.default_color = COL_TRANSPARENT;

	struct cea_decoder_dtvcc_settings settings_708 = {0};
	settings_708.report = ctx->report_708;
	settings_708.enabled = opts ? opts->enable_708 : 1;
	settings_708.active_services_count = 0;
	settings_708.print_file_reports = 0;
	settings_708.no_rollup = opts ? opts->no_rollup : 0;

	/* Enable 708 services */
	if (opts && opts->enable_708)
	{
		for (int i = 0; i < 63; i++)
		{
			settings_708.services_enabled[i] = opts->services_708[i];
			if (opts->services_708[i])
				settings_708.active_services_count++;
		}
	}
	else if (!opts || opts->enable_708)
	{
		/* Default: enable service 1 */
		settings_708.services_enabled[0] = 1;
		settings_708.active_services_count = 1;
	}

	struct cea_decoders_common_settings_t dec_settings = {0};
	dec_settings.settings_608 = &settings_608;
	dec_settings.settings_dtvcc = &settings_708;
	dec_settings.cc_channel = opts ? opts->cc_channel : 1;
	dec_settings.extract = 1; /* Extract field 1 by default */
	dec_settings.no_rollup = opts ? opts->no_rollup : 0;

	ctx->dec = init_cc_decode(&dec_settings);
	if (!ctx->dec)
	{
		free(ctx);
		return NULL;
	}

	ctx->timing = ctx->dec->timing;
	ctx->reorder_window_override = opts ? opts->reorder_window : 0;
	memset(&ctx->sub, 0, sizeof(ctx->sub));
	memset(&ctx->sub_708, 0, sizeof(ctx->sub_708));

	return ctx;
}

cea_ctx *cea_init_default(void)
{
	cea_options opts = {0};
	opts.cc_channel = 1;
	opts.enable_708 = 1;
	opts.services_708[0] = 1; /* Enable service 1 */
	opts.no_rollup = 0;
	return cea_init(&opts);
}

void cea_free(cea_ctx *ctx)
{
	if (!ctx)
		return;

	clear_caption_storage(ctx);
	free(ctx->captions);
	free(ctx->reorder_buf);
	free_sub_chain(&ctx->sub);
	free_sub_chain(&ctx->sub_708);

	if (ctx->dec)
		dinit_cc_decode(&ctx->dec);

	free(ctx->report_608);
	free(ctx->report_708);
	free(ctx);
}

int cea_feed(cea_ctx *ctx, const unsigned char *cc_data, int cc_count, int64_t pts_ms)
{
	if (!ctx || !ctx->dec || !cc_data || cc_count <= 0)
		return -1;

	/* Convert pts_ms to PTS ticks (90kHz clock) */
	int64_t pts_ticks = (int64_t)pts_ms * 90;

	/* Set timing.
	 * For raw cc_data injection we have no frame type info, so tell the
	 * timing system this is an I-frame.  This lets set_fts() immediately
	 * set min_pts on the first call instead of waiting for
	 * FALLBACK_THRESHOLD (100) frames of unknown type.
	 */
	ctx->timing->current_picture_coding_type = CEA_FRAME_TYPE_I_FRAME;
	set_current_pts(ctx->timing, pts_ticks);
	set_fts(ctx->timing);

	/* Point 708 decoder at its own sub chain so it doesn't collide
	 * with the 608 sub (which uses realloc on sub->data).  The 708
	 * decoder uses dtvcc->current_sub set here; process_cc_data will
	 * override it, so we also need the matching change there. */
	if (ctx->dec->dtvcc)
		ctx->dec->dtvcc->current_sub = &ctx->sub_708;

	/* Process cc_data -- 608 output goes to ctx->sub,
	 * 708 output goes to ctx->sub_708 via dtvcc->current_sub */
	int ret = process_cc_data(ctx->dec, (unsigned char *)cc_data, cc_count, &ctx->sub);

	fire_live_callbacks(ctx);

	return ret;
}

/* Sort the reorder buffer by PTS and feed all entries via cea_feed */
static void flush_reorder_buffer(cea_ctx *ctx)
{
	if (ctx->reorder_count == 0)
		return;

	/* Insertion sort by PTS (buffer is typically 2-5 entries) */
	for (int i = 1; i < ctx->reorder_count; i++) {
		struct cc_reorder_entry key = ctx->reorder_buf[i];
		int j = i - 1;
		while (j >= 0 && ctx->reorder_buf[j].pts_ms > key.pts_ms) {
			ctx->reorder_buf[j + 1] = ctx->reorder_buf[j];
			j--;
		}
		ctx->reorder_buf[j + 1] = key;
	}

	/* Feed each entry in PTS order */
	for (int i = 0; i < ctx->reorder_count; i++) {
		cea_feed(ctx, ctx->reorder_buf[i].cc_data,
		              ctx->reorder_buf[i].cc_count,
		              ctx->reorder_buf[i].pts_ms);
	}

	ctx->reorder_count = 0;
}

int cea_set_demuxer(cea_ctx *ctx, cea_codec_type codec,
                    cea_packaging_type packaging,
                    const unsigned char *extradata, int extradata_size)
{
	if (!ctx)
		return -1;

	/* MPEG-2 always uses Annex B start codes */
	if (codec == CEA_CODEC_MPEG2 && packaging == CEA_PACKAGING_AVCC)
		return -1;

	ctx->codec = codec;
	ctx->packaging = packaging;
	ctx->nal_length_size = 0;
	ctx->max_reorder_frames = -1;
	ctx->demuxer_configured = 1;

	/* Try to parse reorder window from extradata (SPS) */
	if (codec == CEA_CODEC_H264 && extradata && extradata_size > 0) {
		int mr = cea_demux_h264_parse_extradata_reorder(extradata, extradata_size);
		if (mr >= 0) {
			ctx->max_reorder_frames = mr;
			mprint("SPS: max_num_reorder_frames=%d\n", mr);
		}
	}

	return 0;
}

int cea_feed_packet(cea_ctx *ctx, const unsigned char *pkt_data,
                         int pkt_size, int64_t pts_ms)
{
	if (!ctx || !pkt_data || pkt_size <= 0 || !ctx->demuxer_configured)
		return -1;

	unsigned char cc_data[31 * 3];
	cea_demux_result result;

	if (ctx->codec == CEA_CODEC_H264) {
		result = cea_demux_h264_extract_cc(
			ctx->packaging == CEA_PACKAGING_AVCC,
			&ctx->nal_length_size,
			pkt_data, pkt_size, cc_data);
	} else {
		result = cea_demux_mpeg2_extract_cc(
			pkt_data, pkt_size, cc_data);
	}

	/* Update reorder window from stream if not yet known */
	if (result.reorder_window >= 0 && ctx->max_reorder_frames < 0)
		ctx->max_reorder_frames = result.reorder_window;

	/* Add cc_data to reorder buffer */
	if (result.cc_count > 0) {
		if (ctx->reorder_count >= ctx->reorder_cap) {
			int new_cap = ctx->reorder_cap ? ctx->reorder_cap * 2 : 8;
			struct cc_reorder_entry *tmp = realloc(ctx->reorder_buf,
				new_cap * sizeof(*tmp));
			if (!tmp)
				return -1;
			ctx->reorder_buf = tmp;
			ctx->reorder_cap = new_cap;
		}
		ctx->reorder_buf[ctx->reorder_count].pts_ms = pts_ms;
		ctx->reorder_buf[ctx->reorder_count].cc_count = result.cc_count;
		memcpy(ctx->reorder_buf[ctx->reorder_count].cc_data, cc_data, result.cc_count * 3);
		ctx->reorder_count++;
	}

	/* Determine reorder window.
	 * Priority: user override > SPS max_num_reorder_frames > default 4. */
	int window;
	if (ctx->reorder_window_override > 0)
		window = ctx->reorder_window_override;
	else if (ctx->max_reorder_frames >= 0)
		window = ctx->max_reorder_frames;
	else
		window = 4;
	while (ctx->reorder_count > window) {
		/* Find entry with smallest PTS */
		int min_idx = 0;
		for (int i = 1; i < ctx->reorder_count; i++) {
			if (ctx->reorder_buf[i].pts_ms < ctx->reorder_buf[min_idx].pts_ms)
				min_idx = i;
		}
		/* Feed it */
		cea_feed(ctx, ctx->reorder_buf[min_idx].cc_data,
		         ctx->reorder_buf[min_idx].cc_count,
		         ctx->reorder_buf[min_idx].pts_ms);
		/* Remove from buffer by swapping with last */
		ctx->reorder_buf[min_idx] = ctx->reorder_buf[ctx->reorder_count - 1];
		ctx->reorder_count--;
	}

	return 0;
}

int cea_flush(cea_ctx *ctx)
{
	if (!ctx || !ctx->dec)
		return -1;

	/* Flush any pending reorder buffer entries */
	flush_reorder_buffer(ctx);

	if (ctx->dec->dtvcc)
		ctx->dec->dtvcc->current_sub = &ctx->sub_708;

	flush_cc_decode(ctx->dec, &ctx->sub);

	/* Drain any captions produced by the flush (e.g. final EDM) */
	fire_live_callbacks(ctx);

	return 0;
}

int cea_get_captions(cea_ctx *ctx, cea_caption *out, int max_captions)
{
	if (!ctx || !out || max_captions <= 0)
		return 0;

	collect_captions(ctx);

	int n = ctx->caption_count < max_captions ? ctx->caption_count : max_captions;
	memcpy(out, ctx->captions, n * sizeof(cea_caption));

	/* Free the subtitle chains now that we've extracted */
	free_sub_chain(&ctx->sub);
	free_sub_chain(&ctx->sub_708);

	return n;
}
