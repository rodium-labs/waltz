#include "gfx.h"

/*
 * Two bands, not one: gfx_flush() draws into the spare while the DMA is still
 * pushing the other, so the panel transfer and the drawing overlap instead of
 * taking turns. The pointer is what every primitive writes through, so nothing
 * below this line has to know which one is live.
 */
static uint16_t band_mem[2][GFX_BAND_W * GFX_H];
static uint16_t *band = band_mem[0];

/*
 * Where the band lands on the panel, and separately the coordinate origin the
 * primitives draw against. They are normally the same; gfx_translate() slides
 * the drawing origin so a whole screen can be painted at an offset without any
 * paint function knowing about it. That is what makes screen transitions
 * possible with no framebuffer.
 */
static int16_t blit_x, blit_y;
static int16_t band_x, band_y, band_w, band_h;

/* Optional extra clip, in drawing coordinates. Wide open unless gfx_clip() is
 * called. The band already bounds everything; this is for callers that must
 * stay inside a rectangle smaller than the region being flushed. */
#define CLIP_OPEN 16384
static int16_t clip_x0 = -CLIP_OPEN, clip_y0 = -CLIP_OPEN;
static int16_t clip_x1 = CLIP_OPEN, clip_y1 = CLIP_OPEN;

/* Armed by gfx_sync_next(), consumed by the next gfx_flush(). */
static bool sync_pending;

static inline int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

/*
 * Ordered dithering for the gradients.
 *
 * RGB565 gives 32 levels of red and blue, so a gradient spanning the panel
 * steps every few rows and the banding is plainly visible - especially now that
 * the whole background is one. Perturbing each pixel by a 4x4 threshold before
 * quantising trades that for a fine stipple the eye integrates away, and costs
 * a handful of cycles per pixel.
 */
static const uint8_t bayer4[16] = {0U, 8U,  2U, 10U, 12U, 4U, 14U, 6U,
                                   3U, 11U, 1U, 9U,  15U, 7U, 13U, 5U};

static inline uint8_t quant(int16_t v, int16_t bias, uint8_t levels) {
  int16_t t = (int16_t)(v + bias);

  if (t < 0) {
    t = 0;
  } else if (t > 255) {
    t = 255;
  }
  return (uint8_t)(((int32_t)t * levels + 127) / 255);
}

/** The four pixels a dithered row repeats, for one colour on one row. */
static void pattern_for(int16_t y, int16_t r8, int16_t g8, int16_t b8,
                        uint16_t *pat) {
  uint8_t k;

  /*
   * The colour is constant along the row and the threshold only depends on
   * x & 3, so the span is four pixels repeating. Working them out once turns
   * the inner loop from six integer divisions per pixel into an indexed store,
   * which is the difference between this costing something and costing nothing.
   */
  for (k = 0U; k < 4U; ++k) {
    int16_t d = (int16_t)bayer4[((y & 3) << 2) | k];

    pat[k] = GFX_PACK(quant(r8, (int16_t)((d - 8) / 2), 31U),
                      quant(g8, (int16_t)((d - 8) / 4), 63U),
                      quant(b8, (int16_t)((d - 8) / 2), 31U));
  }
}

/** Lay a four pixel pattern across a row of the band. */
static void span_pat(int16_t x, int16_t y, int16_t w, const uint16_t *pat) {
  int16_t x0 = max16(max16(x, band_x), clip_x0);
  int16_t x1 = min16(min16((int16_t)(x + w), (int16_t)(band_x + band_w)), clip_x1);
  uint16_t *row;
  int16_t xx;

  if (y < band_y || y >= band_y + band_h || y < clip_y0 || y >= clip_y1) {
    return;
  }

  row = &band[(y - band_y) * band_w];
  if (pat[0] == pat[1] && pat[0] == pat[2] && pat[0] == pat[3]) {
    /* The colour landed on a representable value, so there is nothing to
     * dither and the row is a plain fill. */
    for (xx = x0; xx < x1; ++xx) {
      row[xx - band_x] = pat[0];
    }
    return;
  }
  for (xx = x0; xx < x1; ++xx) {
    row[xx - band_x] = pat[xx & 3];
  }
}

/** Write one row of a gradient, dithering the 8-bit channels down to 5/6/5. */
static void span_dither(int16_t x, int16_t y, int16_t w, int16_t r8,
                        int16_t g8, int16_t b8) {
  uint16_t pat[4];

  pattern_for(y, r8, g8, b8, pat);
  span_pat(x, y, w, pat);
}

/** Channel values of a packed pixel, widened back to 8 bits. */
static void unpack8(uint16_t c, int16_t *r, int16_t *g, int16_t *b) {
  *r = (int16_t)((GFX_GET_R(c) * 255U) / 31U);
  *g = (int16_t)((GFX_GET_G(c) * 255U) / 63U);
  *b = (int16_t)((GFX_GET_B(c) * 255U) / 31U);
}

/** One channel, @p a to @p b by @p t 255ths. */
static inline int16_t lerp8(int16_t a, int16_t b, uint8_t t) {
  return (int16_t)(a + (((int32_t)(b - a) * t) / 255));
}

/**
 * The two ends of a gradient, unpacked once.
 *
 * Unpacking used to happen per row, which is six divisions a row for two
 * colours that never change. It did not matter while bands were full width and
 * a gradient was a handful of rows; with narrow bands the same rows are walked
 * once per band and it started to show.
 */
typedef struct {
  int16_t r0, g0, b0;
  int16_t r1, g1, b1;
} grad_t;

static void grad_ends(grad_t *e, uint16_t top, uint16_t bottom) {
  unpack8(top, &e->r0, &e->g0, &e->b0);
  unpack8(bottom, &e->r1, &e->g1, &e->b1);
}

static void grad_row(const grad_t *e, uint8_t t, int16_t *r, int16_t *g,
                     int16_t *b) {
  *r = lerp8(e->r0, e->r1, t);
  *g = lerp8(e->g0, e->g1, t);
  *b = lerp8(e->b0, e->b1, t);
}

/**
 * True when nothing between @p x and x+w can land in this band.
 *
 * Bands are narrow columns, so most of what a paint function draws misses most
 * of them. Without this each primitive still walks its whole geometry per band
 * writing nothing, which is what turned a nine band frame into four times the
 * drawing of a one band frame.
 */
static inline bool band_skips_x(int16_t x, int16_t w) {
  int16_t right = (int16_t)(x + w);

  return (x >= (int16_t)(band_x + band_w)) || (right <= band_x) ||
         (x >= clip_x1) || (right <= clip_x0);
}

/*
 * Gradient rows, remembered.
 *
 * A row costs a colour interpolation and four quantisations, and with narrow
 * bands the same rows get worked out once per band - five times over for
 * anything full width. They are worked out again for every panel that happens
 * to share a colour pair, which on the home screen is four of the five tiles.
 *
 * The key is the gradient itself, so an entry can never go stale: the same four
 * numbers always describe the same rows. A hit turns a row into a table read.
 */
#define GRAD_SLOTS 4

typedef struct {
  uint16_t top, bottom;
  int16_t y, h;
  bool valid;
  uint16_t pat[GFX_H][4];
} grad_cache_t;

static grad_cache_t grad_cache[GRAD_SLOTS];
static uint8_t grad_next;

static grad_cache_t *grad_rows(int16_t y, int16_t h, uint16_t top,
                               uint16_t bottom) {
  grad_cache_t *c;
  grad_t ends;
  int16_t yy;
  uint8_t i;

  if (h <= 1 || h > GFX_H) {
    return NULL; /* will not fit the table - the caller falls back */
  }

  for (i = 0U; i < GRAD_SLOTS; ++i) {
    c = &grad_cache[i];
    if (c->valid && c->top == top && c->bottom == bottom && c->y == y &&
        c->h == h) {
      return c;
    }
  }

  /* Round robin. Everything drawn in one frame that matters is in flight at
   * once, so age is as good a choice as any and costs nothing to track. */
  c = &grad_cache[grad_next];
  grad_next = (uint8_t)((grad_next + 1U) % GRAD_SLOTS);

  c->top = top;
  c->bottom = bottom;
  c->y = y;
  c->h = h;
  grad_ends(&ends, top, bottom);
  for (yy = 0; yy < h; ++yy) {
    uint8_t t = (uint8_t)(((int32_t)yy * 255) / (h - 1));
    int16_t r, g, b;

    grad_row(&ends, t, &r, &g, &b);
    pattern_for((int16_t)(y + yy), r, g, b, c->pat[yy]);
  }
  c->valid = true;
  return c;
}

/** Integer square root, enough range for the radii used here. */
static int16_t isqrt(int32_t v) {
  int32_t r = 0;
  int32_t bit = 1L << 14;

  if (v <= 0) {
    return 0;
  }
  while (bit > v) {
    bit >>= 2;
  }
  while (bit) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (int16_t)r;
}

void gfx_flush(int16_t x, int16_t y, int16_t w, int16_t h, gfx_paint_fn paint,
               void *ud) {
  static uint8_t slot;
  int16_t xx;

  if (x < 0) {
    w = (int16_t)(w + x);
    x = 0;
  }
  if (y < 0) {
    h = (int16_t)(h + y);
    y = 0;
  }
  if (x + w > GFX_W) {
    w = (int16_t)(GFX_W - x);
  }
  if (y + h > GFX_H) {
    h = (int16_t)(GFX_H - y);
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  /* One sync per UI frame, not per flush: a screen made of several regions is
   * still one frame, and waiting for the scan before each of them would cost a
   * whole panel refresh apiece. */
  if (sync_pending) {
    sync_pending = false;
    st7789_wait_vblank();
  }

  for (xx = x; xx < x + w; xx = (int16_t)(xx + GFX_BAND_W)) {
    gfx_clip_reset();
    band = band_mem[slot];
    blit_x = xx;
    blit_y = y;
    band_x = xx;
    band_y = y;
    band_w = min16(GFX_BAND_W, (int16_t)(x + w - xx));
    band_h = h;

    paint(ud);
    /* The previous band has had the whole of this one's drawing time to get
     * down the wire, so this usually returns immediately. */
    st7789_blit_wait();
    st7789_blit_start((uint16_t)blit_x, (uint16_t)blit_y, (uint16_t)band_w,
                      (uint16_t)band_h, band);
    slot ^= 1U;
  }
  st7789_blit_wait();
}

void gfx_sync_next(void) { sync_pending = true; }

bool gfx_band_hits(int16_t x, int16_t w) {
  /* Screen coordinates, so this is asked before gfx_translate() moves the
   * drawing origin - the point is to decide whether to draw at all. */
  return (x < (int16_t)(blit_x + band_w)) && ((int16_t)(x + w) > blit_x);
}

void gfx_clip(int16_t x, int16_t y, int16_t w, int16_t h) {
  clip_x0 = x;
  clip_y0 = y;
  clip_x1 = (int16_t)(x + w);
  clip_y1 = (int16_t)(y + h);
}

void gfx_clip_reset(void) {
  clip_x0 = -CLIP_OPEN;
  clip_y0 = -CLIP_OPEN;
  clip_x1 = CLIP_OPEN;
  clip_y1 = CLIP_OPEN;
}

void gfx_translate(int16_t dx, int16_t dy) {
  band_x = (int16_t)(blit_x - dx);
  band_y = (int16_t)(blit_y - dy);
}

void gfx_clear(uint16_t color) { st7789_fill(0, 0, GFX_W, GFX_H, color); }

/* Primitives -------------------------------------------------------------- */

void gfx_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (band_skips_x(x, w)) {
    return;
  }

  int16_t x0 = max16(max16(x, band_x), clip_x0);
  int16_t y0 = max16(max16(y, band_y), clip_y0);
  int16_t x1 = min16(min16((int16_t)(x + w), (int16_t)(band_x + band_w)), clip_x1);
  int16_t y1 = min16(min16((int16_t)(y + h), (int16_t)(band_y + band_h)), clip_y1);
  int16_t yy;

  for (yy = y0; yy < y1; ++yy) {
    uint16_t *p = &band[(yy - band_y) * band_w + (x0 - band_x)];
    int16_t n = (int16_t)(x1 - x0);
    while (n-- > 0) {
      *p++ = color;
    }
  }
}

void gfx_pixel(int16_t x, int16_t y, uint16_t color) {
  if (x < band_x || y < band_y || x >= band_x + band_w ||
      y >= band_y + band_h) {
    return;
  }
  if (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1) {
    return;
  }
  band[(y - band_y) * band_w + (x - band_x)] = color;
}

void gfx_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
  gfx_fill(x, y, w, 1, color);
}

void gfx_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
  gfx_fill(x, y, 1, h, color);
}

void gfx_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  int16_t lo = (x0 < x1) ? x0 : x1;
  int16_t hi = (x0 < x1) ? x1 : x0;
  int16_t dx = (int16_t)(x1 > x0 ? x1 - x0 : x0 - x1);
  int16_t dy = (int16_t)(y1 > y0 ? y1 - y0 : y0 - y1);
  int16_t sx = (int16_t)(x0 < x1 ? 1 : -1);
  int16_t sy = (int16_t)(y0 < y1 ? 1 : -1);
  int16_t err = (int16_t)(dx - dy);

  if (band_skips_x(lo, (int16_t)(hi - lo + 1))) {
    return;
  }

  for (;;) {
    gfx_pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int16_t e2 = (int16_t)(err * 2);
    if (e2 > -dy) {
      err = (int16_t)(err - dy);
      x0 = (int16_t)(x0 + sx);
    }
    if (e2 < dx) {
      err = (int16_t)(err + dx);
      y0 = (int16_t)(y0 + sy);
    }
  }
}

void gfx_vgrad(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t top,
               uint16_t bottom) {
  grad_cache_t *rows;
  grad_t ends;
  if (band_skips_x(x, w)) {
    return;
  }

  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t yy;

  if (h <= 1) {
    gfx_fill(x, y, w, h, top);
    return;
  }

  rows = grad_rows(y, h, top, bottom);
  if (rows != NULL) {
    for (yy = y0; yy < y1; ++yy) {
      span_pat(x, yy, w, rows->pat[yy - y]);
    }
    return;
  }

  grad_ends(&ends, top, bottom);
  for (yy = y0; yy < y1; ++yy) {
    uint8_t t = (uint8_t)(((int32_t)(yy - y) * 255) / (h - 1));
    int16_t r, g, b;

    grad_row(&ends, t, &r, &g, &b);
    span_dither(x, yy, w, r, g, b);
  }
}

/** Horizontal inset of a rounded-rect row, 0 outside the corner arcs. */
static int16_t rrect_inset(int16_t row, int16_t y, int16_t h, int16_t r) {
  int16_t k = -1;

  if (row < y + r) {
    k = (int16_t)(row - y);
  } else if (row >= y + h - r) {
    k = (int16_t)((y + h - 1) - row);
  }
  if (k < 0) {
    return 0;
  }
  return (int16_t)(r - isqrt((int32_t)r * r - (int32_t)(r - k) * (r - k)));
}

void gfx_rrect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
               uint16_t color) {
  gfx_rrect_grad(x, y, w, h, r, color, color);
}

void gfx_rrect_grad(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                    uint16_t top, uint16_t bottom) {
  grad_cache_t *rows = NULL;
  grad_t ends;
  if (band_skips_x(x, w)) {
    return;
  }

  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t yy;

  if (r * 2 > w) {
    r = (int16_t)(w / 2);
  }
  if (r * 2 > h) {
    r = (int16_t)(h / 2);
  }
  if (h > 1 && top != bottom) {
    rows = grad_rows(y, h, top, bottom);
    if (rows == NULL) {
      grad_ends(&ends, top, bottom);
    }
  }
  for (yy = y0; yy < y1; ++yy) {
    int16_t inset = rrect_inset(yy, y, h, r);

    if (h > 1 && top != bottom) {
      int16_t sx = (int16_t)(x + inset);
      int16_t sw = (int16_t)(w - 2 * inset);

      if (rows != NULL) {
        span_pat(sx, yy, sw, rows->pat[yy - y]);
      } else {
        uint8_t t = (uint8_t)(((int32_t)(yy - y) * 255) / (h - 1));
        int16_t cr, cg, cb;

        grad_row(&ends, t, &cr, &cg, &cb);
        span_dither(sx, yy, sw, cr, cg, cb);
      }
    } else {
      gfx_fill((int16_t)(x + inset), yy, (int16_t)(w - 2 * inset), 1, top);
    }
  }
}

void gfx_rrect_frame(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                     int16_t t, uint16_t color, uint16_t inner) {
  gfx_rrect(x, y, w, h, r, color);
  gfx_rrect((int16_t)(x + t), (int16_t)(y + t), (int16_t)(w - 2 * t),
            (int16_t)(h - 2 * t), (int16_t)(r > t ? r - t : 0), inner);
}

/**
 * Half-chord of a circle of radius @p r at vertical offset @p dy, rounded to
 * the nearest pixel. Flooring here is what makes small discs look like
 * diamonds, so the sqrt is taken at double scale and rounded on the way back.
 */
static int16_t half_chord(int16_t r, int16_t dy) {
  int32_t v = (int32_t)r * r - (int32_t)dy * dy;

  if (v <= 0) {
    return 0;
  }
  return (int16_t)((isqrt(4 * v) + 1) / 2);
}

void gfx_rrect_ring(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                    int16_t t, uint16_t color) {
  if (band_skips_x(x, w)) {
    return;
  }

  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t ir;
  int16_t yy;

  if (r * 2 > w) {
    r = (int16_t)(w / 2);
  }
  if (r * 2 > h) {
    r = (int16_t)(h / 2);
  }
  ir = (int16_t)((r > t) ? (r - t) : 0);

  for (yy = y0; yy < y1; ++yy) {
    int16_t outer = rrect_inset(yy, y, h, r);

    if (yy < y + t || yy >= y + h - t) {
      gfx_fill((int16_t)(x + outer), yy, (int16_t)(w - 2 * outer), 1, color);
    } else {
      int16_t inner =
          (int16_t)(rrect_inset(yy, (int16_t)(y + t), (int16_t)(h - 2 * t), ir) +
                    t);
      gfx_fill((int16_t)(x + outer), yy, (int16_t)(inner - outer), 1, color);
      gfx_fill((int16_t)(x + w - inner), yy, (int16_t)(inner - outer), 1,
               color);
    }
  }
}

void gfx_disc(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  if (band_skips_x((int16_t)(cx - r), (int16_t)(2 * r + 1))) {
    return;
  }

  int16_t y0 = max16((int16_t)(cy - r), band_y);
  int16_t y1 = min16((int16_t)(cy + r + 1), (int16_t)(band_y + band_h));
  int16_t yy;

  for (yy = y0; yy < y1; ++yy) {
    int16_t half = half_chord(r, (int16_t)(yy - cy));
    gfx_fill((int16_t)(cx - half), yy, (int16_t)(2 * half + 1), 1, color);
  }
}

void gfx_ring(int16_t cx, int16_t cy, int16_t r_out, int16_t r_in,
              uint16_t color) {
  if (band_skips_x((int16_t)(cx - r_out), (int16_t)(2 * r_out + 1))) {
    return;
  }

  int16_t y0 = max16((int16_t)(cy - r_out), band_y);
  int16_t y1 = min16((int16_t)(cy + r_out + 1), (int16_t)(band_y + band_h));
  int16_t yy;

  for (yy = y0; yy < y1; ++yy) {
    int16_t dy = (int16_t)(yy - cy);
    int16_t outer = half_chord(r_out, dy);

    if (dy > r_in || dy < -r_in) {
      /* row misses the hole entirely - one solid span */
      gfx_fill((int16_t)(cx - outer), yy, (int16_t)(2 * outer + 1), 1, color);
    } else {
      int16_t hole = half_chord(r_in, dy);
      gfx_fill((int16_t)(cx - outer), yy, (int16_t)(outer - hole), 1, color);
      gfx_fill((int16_t)(cx + hole + 1), yy, (int16_t)(outer - hole), 1, color);
    }
  }
}

void gfx_circle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  if (band_skips_x((int16_t)(cx - r), (int16_t)(2 * r + 1))) {
    return;
  }

  int16_t x = 0;
  int16_t y = r;
  int16_t d = (int16_t)(3 - 2 * r);

  if (r <= 0) {
    gfx_pixel(cx, cy, color);
    return;
  }

  while (x <= y) {
    gfx_pixel((int16_t)(cx + x), (int16_t)(cy + y), color);
    gfx_pixel((int16_t)(cx - x), (int16_t)(cy + y), color);
    gfx_pixel((int16_t)(cx + x), (int16_t)(cy - y), color);
    gfx_pixel((int16_t)(cx - x), (int16_t)(cy - y), color);
    gfx_pixel((int16_t)(cx + y), (int16_t)(cy + x), color);
    gfx_pixel((int16_t)(cx - y), (int16_t)(cy + x), color);
    gfx_pixel((int16_t)(cx + y), (int16_t)(cy - x), color);
    gfx_pixel((int16_t)(cx - y), (int16_t)(cy - x), color);

    if (d < 0) {
      d = (int16_t)(d + 4 * x + 6);
    } else {
      d = (int16_t)(d + 4 * (x - y) + 10);
      --y;
    }
    ++x;
  }
}

void gfx_tri(int16_t x, int16_t y, int16_t w, int16_t h, gfx_tri_dir_t dir,
             uint16_t color) {
  if (band_skips_x(x, w)) {
    return;
  }

  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t span = (int16_t)(h - 1);
  int16_t yy;

  if (span <= 0) {
    gfx_fill(x, y, w, h, color);
    return;
  }
  for (yy = y0; yy < y1; ++yy) {
    /* distance from the vertical centre, doubled to stay in integers */
    int16_t d2 = (int16_t)(2 * (yy - y) - span);
    if (d2 < 0) {
      d2 = (int16_t)-d2;
    }
    /* 1 px at the base corners, full width at the centre row */
    int16_t len = (int16_t)(1 + ((int32_t)(w - 1) * (span - d2)) / span);
    if (len <= 0) {
      continue;
    }
    if (dir == GFX_TRI_RIGHT) {
      gfx_fill(x, yy, len, 1, color);
    } else {
      gfx_fill((int16_t)(x + w - len), yy, len, 1, color);
    }
  }
}

void gfx_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *bits,
                uint16_t color) {
  if (band_skips_x(x, w)) {
    return;
  }

  int16_t stride = (int16_t)((w + 7) / 8);
  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t yy;

  for (yy = y0; yy < y1; ++yy) {
    const uint8_t *row = &bits[(yy - y) * stride];
    int16_t xx;
    for (xx = 0; xx < w; ++xx) {
      if (row[xx >> 3] & (0x80U >> (xx & 7))) {
        gfx_pixel((int16_t)(x + xx), yy, color);
      }
    }
  }
}

int16_t gfx_text(int16_t x, int16_t y, const gfx_font_t *f, const char *s,
                 uint16_t color) {
  int16_t pen = x;

  for (; *s; ++s) {
    uint8_t ch = (uint8_t)*s;
    const uint16_t *glyph;
    uint8_t advance;
    int16_t row;

    if (ch < 32U || ch > 126U) {
      ch = '?';
    }
    glyph = &f->data[(ch - 32U) * f->height];
    advance = f->char_width ? f->char_width[ch - 32U] : f->width;

    /* Skip glyphs that cannot land in the band at all. */
    if (pen + f->width > band_x && pen < band_x + band_w &&
        y + f->height > band_y && y < band_y + band_h) {
      for (row = 0; row < f->height; ++row) {
        uint16_t bits = glyph[row];
        int16_t col;
        if (!bits) {
          continue;
        }
        for (col = 0; col < f->width; ++col) {
          if ((bits << col) & 0x8000U) {
            gfx_pixel((int16_t)(pen + col), (int16_t)(y + row), color);
          }
        }
      }
    }
    pen = (int16_t)(pen + advance);
  }
  return (int16_t)(pen - x);
}

int16_t gfx_text_w(const gfx_font_t *f, const char *s) {
  int16_t w = 0;

  for (; *s; ++s) {
    uint8_t ch = (uint8_t)*s;
    if (ch < 32U || ch > 126U) {
      ch = '?';
    }
    w = (int16_t)(w + (f->char_width ? f->char_width[ch - 32U] : f->width));
  }
  return w;
}

/* Colour helpers ---------------------------------------------------------- */

uint16_t gfx_mix(uint16_t a, uint16_t b, uint8_t t) {
  uint32_t ia = 255U - t;
  uint32_t r = ((GFX_GET_R(a) * ia + GFX_GET_R(b) * t) + 127U) / 255U;
  uint32_t g = ((GFX_GET_G(a) * ia + GFX_GET_G(b) * t) + 127U) / 255U;
  uint32_t bl = ((GFX_GET_B(a) * ia + GFX_GET_B(b) * t) + 127U) / 255U;

  return GFX_PACK(r, g, bl);
}

uint16_t gfx_dim(uint16_t color, uint8_t t) {
  return gfx_mix(0x0000U, color, t);
}
