/**
 * @file    gfx.h
 * @brief   Banded RGB565 renderer for the 284x76 panel.
 *
 * A full framebuffer for this panel would be 284*76*2 = 42 kB out of the
 * F401's 64 kB, which leaves nothing for an MP3 decoder later on. Instead the
 * screen is painted a vertical strip at a time through a pair of
 * GFX_BAND_W x GFX_H scratch buffers (4.8 kB each).
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
 * pushes each band to the panel, drawing the next while the last is still on the
 * wire. All coordinates stay in absolute panel space;
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

/**
 * Columns per band. 32 * 76 * 2 = 4864 bytes of scratch, two of them.
 *
 * Bands run down the screen rather than across it, which looks like the wrong
 * way round until you know which way the panel scans. In landscape the 284 px
 * dimension is the one laid on the controller's gate lines, so the scan sweeps
 * sideways. A band that spanned the full width would touch every scan line, and
 * the last band would always land behind the beam no matter when the frame
 * started - which is exactly the tear that survived the first vsync attempt.
 * Splitting by column instead means each band is a slice of the scan's path,
 * and the write walks the same way the beam does, staying in front of it.
 *
 * Thirty-two rather than sixty-four for two reasons, both measured. It halves
 * the pair of buffers from 19 kB to 9.7 kB, which is what lets an MP3 decoder
 * fit at all - Helix plus a PCM double buffer plus FatFs came out about 2 kB
 * over the top at 64. And it is *faster*: a narrower band means a shorter
 * transfer to hide the drawing behind, so less of the last one is left over.
 * That was not true before gradient rows were cached and primitives learned to
 * skip a band they cannot reach; at that point more bands meant redrawing the
 * screen more times, and 32 measured 17 ms against 64's 13.
 *
 * More bands also buys deadline, which is the opposite of the obvious. The write
 * does not have to finish before the scan has crossed the strip - it has to stay
 * ahead of it band by band, so with N bands the frame has until
 * porch + crossing * (N-1)/N. Nine bands allow 15.3 ms where five allow 14.1,
 * and a screen transition - two screens in flight at once - needs about 15.
 * Six bands is the one width that fails: 14.6 ms against a 14.5 ms deadline,
 * and it tore.
 */
#define GFX_BAND_W 32

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

/**
 * @brief Line the next flush up with the panel's scanout.
 *
 * Call once per UI frame, before any drawing. The first gfx_flush() after it
 * waits for the scan to leave the visible strip, so the whole frame goes down
 * ahead of the beam; later flushes in the same frame are already inside that
 * window and do not wait again.
 */
void gfx_sync_next(void);

/**
 * @brief Does the span @p x..x+w reach the band being painted?
 *
 * For callers that can skip something far larger than a primitive - a whole
 * screen mid-transition, say. Screen coordinates, asked before gfx_translate().
 */
bool gfx_band_hits(int16_t x, int16_t w);

/**
 * @brief Shift the coordinate origin the primitives draw against.
 *
 * Valid only inside a paint callback, and it may be called more than once per
 * callback - which is the point: a transition paints the outgoing screen at one
 * offset and the incoming one at another, without either screen's paint
 * function knowing it is being moved. Reset to (0, 0) at the start of every
 * band, so a callback that never calls it behaves exactly as before.
 */
void gfx_translate(int16_t dx, int16_t dy);

/**
 * @brief Restrict drawing to a rectangle inside the region being flushed.
 *
 * Clipping normally falls out of the band for free, which is fine while every
 * paint function owns exactly the region it is flushed with. It stops being
 * free the moment one is reused inside a larger flush - a marquee drawn as part
 * of a whole-screen repaint would otherwise spill its text across the screen.
 * Reset at the start of every band.
 */
void gfx_clip(int16_t x, int16_t y, int16_t w, int16_t h);
void gfx_clip_reset(void);

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

/**
 * @brief Rounded-rect outline of thickness @p t that touches nothing inside it.
 *
 * Unlike gfx_rrect_frame() this leaves the interior alone, so it can be drawn
 * over artwork - which is what a focus ring has to do.
 */
void gfx_rrect_ring(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                    int16_t t, uint16_t color);

void gfx_disc(int16_t cx, int16_t cy, int16_t r, uint16_t color);
/** Annulus from @p r_in (exclusive) to @p r_out (inclusive). */
void gfx_ring(int16_t cx, int16_t cy, int16_t r_out, int16_t r_in,
              uint16_t color);
/**
 * @brief One pixel wide circle outline.
 *
 * Not the same as gfx_ring() with r_in = r_out - 1. That is a true annulus, and
 * near the top and bottom a scanline crosses it almost horizontally, so it
 * flares into a wide cap. This strokes the curve instead, so the line stays one
 * pixel all the way round.
 */
void gfx_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
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
