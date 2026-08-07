/**
 * @file    gfx.h
 * @brief   Banded RGB565 renderer for the 284x76 panel.
 *
 * A full framebuffer for this panel would be 284*76*2 = 42 kB out of the
 * F401's 64 kB, which leaves nothing for an MP3 decoder later on. Instead the
 * screen is painted a horizontal strip at a time through a single
 * GFX_W x GFX_BAND_H scratch buffer (9 kB).
 *
 * Everything is drawn from inside a paint callback:
 *
 *     static void paint_thing(void *ud) {
 *         gfx_fill(190, 20, 88, 20, BG);
 *         gfx_text(194, 26, &Font_Mono6x8, "hello", FG);
 *     }
 *     gfx_flush(190, 20, 88, 20, paint_thing, NULL);
 *
 * gfx_flush() splits the rectangle into bands, calls @c paint once per band and
 * pushes each band to the panel. All coordinates stay in absolute panel space;
 * anything falling outside the current band is clipped away, which is also how
 * marquees get their clipping for free.
 */

#ifndef __GFX_H__
#define __GFX_H__

#include <stdbool.h>
#include <stdint.h>

#include "gfx_fonts.h"
#include "st7789.h"

#define GFX_W ST7789_W
#define GFX_H ST7789_H

/** Rows per band. 284 * 16 * 2 = 9088 bytes of scratch. */
#define GFX_BAND_H 16

/**
 * Pixels are DMAed out of the band buffer byte by byte, and this core is
 * little-endian, so the low byte of each stored halfword hits the wire first.
 * ST7789 wants the high byte of an RGB565 pixel first, so what gets stored has
 * to be pre-swapped - that is what this does.
 *
 * Set to 0 for a panel that wants the other order. Ui_ColorSweep() tries both
 * at runtime, so there is no need to guess.
 */
#define GFX_WIRE_SWAP 1

#if GFX_WIRE_SWAP
/** Convert between logical RGB565 and stored wire order. Self-inverse. */
#define GFX_WIRE(v)                                                            \
  ((uint16_t)((((uint16_t)(v) & 0x00FFU) << 8) | ((uint16_t)(v) >> 8)))
#else
#define GFX_WIRE(v) ((uint16_t)(v))
#endif

/** Pack 8:8:8 into a stored pixel. */
#define GFX_RGB(r, g, b)                                                       \
  GFX_WIRE((((uint16_t)(r) & 0xF8U) << 8) | (((uint16_t)(g) & 0xFCU) << 3) |   \
           ((uint16_t)(b) >> 3))

/** Unpack the 5/6/5 channels of a stored pixel. */
#define GFX_GET_R(c) ((GFX_WIRE(c) >> 11) & 0x1FU)
#define GFX_GET_G(c) ((GFX_WIRE(c) >> 5) & 0x3FU)
#define GFX_GET_B(c) (GFX_WIRE(c) & 0x1FU)

/** Repack from 5/6/5 channel values into a stored pixel. */
#define GFX_PACK(r, g, b)                                                      \
  GFX_WIRE(((uint16_t)(r) << 11) | ((uint16_t)(g) << 5) | (uint16_t)(b))

/** Direction for gfx_tri(). */
typedef enum { GFX_TRI_RIGHT = 0, GFX_TRI_LEFT } gfx_tri_dir_t;

/** Paint callback invoked once per band by gfx_flush(). */
typedef void (*gfx_paint_fn)(void *ud);

/**
 * @brief Render a rectangle of the screen and push it to the panel.
 * @param x,y,w,h Region in panel coordinates; clipped to the panel.
 * @param paint   Called once per band. Must repaint every pixel it owns.
 * @param ud      Passed through to @p paint.
 */
void gfx_flush(int16_t x, int16_t y, int16_t w, int16_t h, gfx_paint_fn paint,
               void *ud);

/** Flood the whole panel. Cheap - goes straight to the controller. */
void gfx_clear(uint16_t color);

/* Primitives - only valid while inside a paint callback ------------------- */

void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void gfx_pixel(int16_t x, int16_t y, uint16_t color);
void gfx_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void gfx_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/** Vertical gradient; interpolated in panel space so bands line up. */
void gfx_vgrad(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t top,
               uint16_t bottom);

void gfx_rrect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
               uint16_t color);
/** Rounded rect filled with a vertical gradient. */
void gfx_rrect_grad(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                    uint16_t top, uint16_t bottom);
/**
 * @brief Rounded-rect outline of thickness @p t.
 * @param inner Colour painted inside the outline - this is a fill-then-inset
 *              fill, so it only looks right over a known flat background.
 */
void gfx_rrect_frame(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                     int16_t t, uint16_t color, uint16_t inner);

void gfx_disc(int16_t cx, int16_t cy, int16_t r, uint16_t color);
/** Annulus from @p r_in (exclusive) to @p r_out (inclusive). */
void gfx_ring(int16_t cx, int16_t cy, int16_t r_out, int16_t r_in,
              uint16_t color);
/** Isoceles triangle filling @p w x @p h, apex on the left or right edge. */
void gfx_tri(int16_t x, int16_t y, int16_t w, int16_t h, gfx_tri_dir_t dir,
             uint16_t color);

/** Draw set bits of a row-major 1bpp bitmap; clear bits stay transparent. */
void gfx_bitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                const uint8_t *bits, uint16_t color);

/** Draw @p s with a transparent background; returns the x advance. */
int16_t gfx_text(int16_t x, int16_t y, const gfx_font_t *f, const char *s,
                 uint16_t color);
/** Pixel width @p s would occupy in @p f. */
int16_t gfx_text_w(const gfx_font_t *f, const char *s);

/* Colour helpers ---------------------------------------------------------- */

/** Linear blend, @p t = 0 gives @p a and 255 gives @p b. */
uint16_t gfx_mix(uint16_t a, uint16_t b, uint8_t t);
/** Scale towards black; @p t = 255 leaves the colour unchanged. */
uint16_t gfx_dim(uint16_t color, uint8_t t);

#endif /* __GFX_H__ */
