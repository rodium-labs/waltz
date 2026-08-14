#include "gfx.h"

/* The one scratch band every primitive writes into. */
static uint16_t band[GFX_W * GFX_BAND_H];

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

static inline int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

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
  int16_t yy;

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

  for (yy = y; yy < y + h; yy = (int16_t)(yy + GFX_BAND_H)) {
    gfx_clip_reset();
    blit_x = x;
    blit_y = yy;
    band_x = x;
    band_y = yy;
    band_w = w;
    band_h = min16(GFX_BAND_H, (int16_t)(y + h - yy));

    paint(ud);
    st7789_blit((uint16_t)blit_x, (uint16_t)blit_y, (uint16_t)band_w,
                (uint16_t)band_h, band);
  }
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
  int16_t dx = (int16_t)(x1 > x0 ? x1 - x0 : x0 - x1);
  int16_t dy = (int16_t)(y1 > y0 ? y1 - y0 : y0 - y1);
  int16_t sx = (int16_t)(x0 < x1 ? 1 : -1);
  int16_t sy = (int16_t)(y0 < y1 ? 1 : -1);
  int16_t err = (int16_t)(dx - dy);

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
  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t yy;

  if (h <= 1) {
    gfx_fill(x, y, w, h, top);
    return;
  }
  for (yy = y0; yy < y1; ++yy) {
    uint8_t t = (uint8_t)(((int32_t)(yy - y) * 255) / (h - 1));
    gfx_fill(x, yy, w, 1, gfx_mix(top, bottom, t));
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
  int16_t y0 = max16(y, band_y);
  int16_t y1 = min16((int16_t)(y + h), (int16_t)(band_y + band_h));
  int16_t yy;

  if (r * 2 > w) {
    r = (int16_t)(w / 2);
  }
  if (r * 2 > h) {
    r = (int16_t)(h / 2);
  }
  for (yy = y0; yy < y1; ++yy) {
    int16_t inset = rrect_inset(yy, y, h, r);
    uint16_t c = top;
    if (h > 1 && top != bottom) {
      c = gfx_mix(top, bottom, (uint8_t)(((int32_t)(yy - y) * 255) / (h - 1)));
    }
    gfx_fill((int16_t)(x + inset), yy, (int16_t)(w - 2 * inset), 1, c);
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

void gfx_tri(int16_t x, int16_t y, int16_t w, int16_t h, gfx_tri_dir_t dir,
             uint16_t color) {
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
