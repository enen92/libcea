/* SPDX-License-Identifier: GPL-2.0-only
 * libcea — Programmatic closed-caption extraction (EIA-608 / CEA-708).
 * Based on CCExtractor: https://github.com/CCExtractor/ccextractor
 *
 * Copyright (C) libcea, 2026–present.
 * Copyright (C) CCExtractor <https://github.com/CCExtractor/ccextractor>, <2026.
 */

/*
 * demo.c - Standalone demo using FFmpeg (avformat) + libcea for caption extraction
 *
 * This program:
 *  1. Uses FFmpeg avformat to open a media file and read raw video packets
 *  2. Feeds compressed packets into libcea's built-in demuxer
 *  3. The library extracts cc_data, reorders B-frames, and decodes EIA-608/708
 *  4. Delivers captions via a live callback as they appear and disappear
 *
 * Build: see libcea/demo/CMakeLists.txt
 * Usage: ./demo <input_file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#include "cea.h"

/* ------------------------------------------------------------------ */
/* Log callback for libcea internal messages                           */
/* ------------------------------------------------------------------ */
static void log_callback(cea_log_level level, const char *msg, void *userdata)
{
	(void)userdata;
	const char *prefix[] = {"[DEBUG]", "[INFO]", "[WARN]", "[ERROR]", "[FATAL]"};
	printf("%s %s\n", prefix[level], msg);
}

/* ------------------------------------------------------------------ */
/* Live caption callback                                               */
/*                                                                     */
/* cap->text != NULL  →  new/updated caption on screen.               */
/*   start_ms: when it appeared; end_ms: 0 (not yet known).           */
/*   → Show this text immediately.                                     */
/*                                                                     */
/* cap->text == NULL  →  caption cleared.                             */
/*   end_ms: when it disappeared.                                      */
/*   → Clear the display at end_ms.                                    */
/* ------------------------------------------------------------------ */
static void live_caption_cb(const cea_caption *cap, void *userdata)
{
	(void)userdata;

	if (cap->text) {
		/* Caption appearing */
		printf("[SHOW] field=%d row=%d mode=%s info=%s start=%lld ms\n",
		       cap->field, cap->base_row, cap->mode, cap->info,
		       (long long)cap->start_ms);
		const char *p = cap->text;
		while (*p) {
			const char *nl = strchr(p, '\n');
			if (nl) {
				printf("       %.*s\n", (int)(nl - p), p);
				p = nl + 1;
			} else {
				printf("       %s\n", p);
				break;
			}
		}
	} else {
		/* Caption disappearing */
		printf("[CLEAR] field=%d end=%lld ms\n",
		       cap->field, (long long)cap->end_ms);
	}

	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
		return 1;
	}

	const char *input_file = argv[1];

	/* ---- FFmpeg setup ---- */
	AVFormatContext *fmt_ctx = NULL;
	int ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL);
	if (ret < 0) {
		char errbuf[128];
		av_strerror(ret, errbuf, sizeof(errbuf));
		fprintf(stderr, "Error: cannot open '%s': %s\n", input_file, errbuf);
		return 1;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "Error: cannot find stream info\n");
		avformat_close_input(&fmt_ctx);
		return 1;
	}

	/* Find the best video stream */
	int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_idx < 0) {
		fprintf(stderr, "Error: no video stream found\n");
		avformat_close_input(&fmt_ctx);
		return 1;
	}

	AVStream *vstream = fmt_ctx->streams[video_idx];
	enum AVCodecID codec_id = vstream->codecpar->codec_id;

	int is_h264  = (codec_id == AV_CODEC_ID_H264);
	int is_mpeg2 = (codec_id == AV_CODEC_ID_MPEG2VIDEO);

	if (!is_h264 && !is_mpeg2) {
		fprintf(stderr, "Warning: video codec is neither H.264 nor MPEG-2 (codec_id=%d).\n"
				"         Caption extraction may not work.\n", codec_id);
	}

	printf("Input: %s\n", input_file);
	printf("Video stream #%d: %s\n", video_idx,
	       is_h264 ? "H.264/AVC" : (is_mpeg2 ? "MPEG-2" : "other"));

	/* Determine packaging format for H.264 */
	int is_avcc = 0;
	if (is_h264 && vstream->codecpar->extradata_size >= 7 &&
	    vstream->codecpar->extradata[0] == 1) {
		is_avcc = 1;
		printf("H.264 AVCC format\n");
	} else if (is_h264) {
		printf("H.264 Annex B format\n");
	}

	/* ---- libcea setup ---- */
	cea_set_log_callback(log_callback, NULL, CEA_LOG_INFO);
	cea_ctx *ctx = cea_init_default();
	if (!ctx) {
		fprintf(stderr, "Error: failed to init libcea\n");
		avformat_close_input(&fmt_ctx);
		return 1;
	}

	/* Configure demuxer */
	cea_codec_type codec = is_h264 ? CEA_CODEC_H264 : CEA_CODEC_MPEG2;
	cea_packaging_type pkg = is_avcc ? CEA_PACKAGING_AVCC : CEA_PACKAGING_ANNEX_B;
	if (cea_set_demuxer(ctx, codec, pkg,
	                    vstream->codecpar->extradata,
	                    vstream->codecpar->extradata_size) < 0) {
		fprintf(stderr, "Error: failed to configure demuxer\n");
		cea_free(ctx);
		avformat_close_input(&fmt_ctx);
		return 1;
	}

	/*
	 * Register the live callback.
	 *
	 * In live/streaming mode the callback fires from within
	 * cea_feed_packet() rather than the caller having to poll
	 * cea_get_captions() after every packet.  This ensures captions
	 * are delivered at the earliest possible moment:
	 *
	 *   SHOW  events fire as soon as text appears on the virtual
	 *         608/708 screen (start_ms known, end_ms still 0).
	 *
	 *   CLEAR events fire when the screen is replaced or erased
	 *         (end_ms known).
	 *
	 * The player should display the text immediately on SHOW and
	 * schedule a clear at end_ms on CLEAR.
	 */
	cea_set_caption_callback(ctx, live_caption_cb, NULL);

	/* ---- Packet reading loop ---- */
	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "Error: cannot allocate AVPacket\n");
		cea_free(ctx);
		avformat_close_input(&fmt_ctx);
		return 1;
	}

	int64_t total_packets = 0;

	while (av_read_frame(fmt_ctx, pkt) >= 0) {
		if (pkt->stream_index == video_idx) {
			/* Convert PTS to milliseconds */
			int64_t pts_ms = 0;
			if (pkt->pts != AV_NOPTS_VALUE) {
				AVRational tb = vstream->time_base;
				pts_ms = av_rescale_q(pkt->pts, tb, (AVRational){1, 1000});
			} else if (pkt->dts != AV_NOPTS_VALUE) {
				AVRational tb = vstream->time_base;
				pts_ms = av_rescale_q(pkt->dts, tb, (AVRational){1, 1000});
			}

			/* Captions are delivered via live_caption_cb() */
			cea_feed_packet(ctx, pkt->data, pkt->size, pts_ms);
			total_packets++;
		}
		av_packet_unref(pkt);
	}

	/* Flush remaining buffered captions (fires final callbacks) */
	cea_flush(ctx);

	/* ---- Summary ---- */
	printf("\n--- Summary ---\n");
	printf("Total video packets read: %lld\n", (long long)total_packets);

	/* ---- Cleanup ---- */
	av_packet_free(&pkt);
	avformat_close_input(&fmt_ctx);
	cea_free(ctx);

	return 0;
}
