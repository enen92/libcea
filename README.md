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
cea_get_captions(ctx, captions, 64);

cea_free(ctx);
```

For a complete working example using FFmpeg for demuxing, see the [demo](demo/) directory.

## License

GPL-2.0-only. See individual source files for copyright details.
