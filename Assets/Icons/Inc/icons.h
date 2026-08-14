#pragma once

#include <stdint.h>

/*
 * Row-major 1bpp bitmaps, MSB first, (w + 7) / 8 bytes per row - the format
 * gfx_bitmap() expects. Everything geometric (play, pause, prev, next,
 * shuffle, repeat, battery) is drawn with primitives instead, so only the
 * genuinely hand-drawn glyphs live here.
 */

/** Eighth note, 7x9. */
extern const uint8_t icon_note[9];
/** Speaker with three arcs, 10x9. */
extern const uint8_t icon_speaker[18];
