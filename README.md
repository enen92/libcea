# libcea

A minimal C library for extracting closed captions (EIA-608 and CEA-708) from video bitstreams.

Based on [CCExtractor](https://github.com/CCExtractor/ccextractor), the open-source tool for closed-caption extraction. libcea isolates the core decoding logic into a small, embeddable static library with a simple C API (`cea.h`).

## Features

- **EIA-608 decoder** -- field 1/2 (CC1-CC4), all caption modes (pop-on, roll-up, paint-on)
- **CEA-708 decoder** -- DTVCC services, with configurable service selection
- **H.264/AVC demuxer** -- extracts cc_data from SEI NAL units (Annex B and AVCC packaging)
- **MPEG-2 demuxer** -- extracts cc_data from user_data (GA94) start codes
- **B-frame reorder buffer** -- PTS-based sliding window, auto-detected from SPS or configurable
- **No external dependencies** -- pure C99, builds as a static library

## Building

Requires CMake 3.16+ and a C99 compiler.

```sh
cd libcea
mkdir build && cd build
cmake ..
make
```

This produces `libcea.a` (static library). Link it into your project and include `cea.h`.

To use libcea as a subdirectory in your own CMake project:

```cmake
add_subdirectory(libcea)
target_link_libraries(your_target cea)
```

## Usage

### Pull mode

Feed packets and retrieve captions by polling after each feed or flush:

```c
#include "cea.h"

/* Initialize with defaults (CC1 + 708 service 1) */
cea_ctx *ctx = cea_init_default();

/* Configure demuxer for your codec */
cea_set_demuxer(ctx, CEA_CODEC_H264, CEA_PACKAGING_AVCC,
                extradata, extradata_size);

/* Feed compressed video packets (decode order is fine) */
cea_feed_packet(ctx, pkt_data, pkt_size, pts_ms);

/* Retrieve decoded captions */
cea_caption captions[64];
int count = cea_get_captions(ctx, captions, 64);

/* Flush remaining captions at end of stream */
cea_flush(ctx);
count = cea_get_captions(ctx, captions, 64);

cea_free(ctx);
```

### Live / streaming mode

Register a callback to receive captions as they appear and disappear, without polling:

```c
void on_caption(const cea_caption *cap, void *userdata)
{
    if (cap->text) {
        /* Caption appeared: show it now, end time not yet known */
        display_caption(cap->text, cap->start_ms);
    } else {
        /* Caption cleared: hide it at cap->end_ms */
        schedule_clear(cap->end_ms);
    }
}

cea_ctx *ctx = cea_init_default();
cea_set_demuxer(ctx, CEA_CODEC_H264, CEA_PACKAGING_AVCC,
                extradata, extradata_size);
cea_set_caption_callback(ctx, on_caption, NULL);

while (have_packets)
    cea_feed_packet(ctx, pkt_data, pkt_size, pts_ms);

cea_flush(ctx);  /* fires any remaining callbacks */
cea_free(ctx);
```

The callback fires from within `cea_feed_packet` and `cea_flush`. `cap->text` is only valid for the duration of the callback — copy it if you need it beyond that.

#### Timing and discontinuities in live mode

`start_ms` is set when the caption first appears on screen; `end_ms` is `0` in the SHOW event and filled in only on the matching CLEAR event. Players should display the caption immediately on SHOW and schedule removal on CLEAR.

A few edge cases to handle:

- **No CLEAR arrives** (e.g. stream ends mid-caption): call `cea_flush()` at end of stream. It will fire any pending CLEAR callbacks. If the stream is cut abruptly without a flush, the player should remove any pending caption after a reasonable display timeout.
- **PTS discontinuities** (seek, channel change, splice): `start_ms` values may jump forward or backward. A SHOW event after a large PTS jump almost certainly belongs to a new segment. If your player tracks the currently displayed caption by `start_ms`, treat a jump of more than a few seconds as a hard reset — clear any pending caption immediately.
- **Roll-up captions** (RU2/RU3/RU4): each scroll step fires a new SHOW event on the same `field`/`channel` pair. The new SHOW replaces the previous one; do not stack them. Use the `field` and `channel` fields to match SHOW and CLEAR events to the right display slot.
- **608 fields arrive separately**: field 1 and field 2 carry independent caption streams. Each fires its own interleaved SHOW/CLEAR events. Maintain a separate display slot per `(field, channel)` pair.

### Selecting channels and services

```c
cea_options opts = {0};
opts.enable_708 = 1;
opts.services_708[0] = 1;  /* 708 service 1 (1-indexed) */
/* services_708[1..62] = 1 to enable additional services */

cea_ctx *ctx = cea_init(&opts);
```

EIA-608 channels (CC1-CC4) are always enabled. The `channel` field in `cea_caption` identifies which channel fired:

| `field` | `channel` | Stream |
|---------|-----------|--------|
| 1       | 1         | CC1    |
| 1       | 2         | CC2    |
| 2       | 1         | CC3    |
| 2       | 2         | CC4    |

### Debug logging

```c
cea_set_log_callback(ctx, my_log_fn, NULL, CEA_LOG_DEBUG);
cea_set_debug_mask(ctx, CEA_DBG_DECODER_608 | CEA_DBG_DECODER_708);
```

Available mask bits (see `cea_debug_mask` in `cea.h`):

| Bit                    | Description                               |
|------------------------|-------------------------------------------|
| `CEA_DBG_DECODER_608`  | EIA-608 MRC, PAC, and command trace       |
| `CEA_DBG_DECODER_708`  | CEA-708 service and window commands       |
| `CEA_DBG_RAW_BLOCKS`   | Raw cc_data triplets as they arrive       |
| `CEA_DBG_VERBOSE`      | General verbose output                    |
| `CEA_DBG_GENERIC_NOTICES` | Miscellaneous decoder notices          |

## License

GPL-2.0-only. See individual source files for copyright details.
