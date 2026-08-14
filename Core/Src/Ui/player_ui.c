#include "player_ui.h"

#include <string.h>

#include "icons.h"
#include "input.h"
#include "main.h"
#include "player.h"
#include "power.h"
#include "settings.h"
#include "theme.h"

/* Marquee ----------------------------------------------------------------- */

#define MARQUEE_BUF 40
/** Milliseconds per scrolled pixel. */
#define MARQUEE_MS 40U
/** Blank run between the end of the text and its repeat. */
#define MARQUEE_GAP 16
/** Pause with the text at its start position before each loop. */
#define MARQUEE_HOLD_MS 1400U

/*
 * Colours are deliberately *not* stored here. They used to be, and switching
 * theme then left the marquee filling with the old background - a visible box
 * behind the title and artist that did not match the rest of the screen. The
 * palette is a runtime lookup now, so anything that caches a COL_* value goes
 * stale the moment the theme changes.
 */
typedef struct {
  int16_t x, y, w, h;
  const gfx_font_t *font;
  bool dim; /**< Draw in the secondary text colour rather than the primary. */
  char text[MARQUEE_BUF];
  int16_t text_w;
  int16_t offset;
  bool scrolling;
  bool dirty;
  uint32_t step_at;
  uint32_t hold_until;
} marquee_t;

/* Player-screen redraw flags ---------------------------------------------- */

#define D_ART (1U << 0)
#define D_TITLE (1U << 1)
#define D_ARTIST (1U << 2)
#define D_METER (1U << 3)
#define D_BAR (1U << 4)
#define D_TIME (1U << 5)
#define D_FORMAT (1U << 6)
#define D_TRANSPORT (1U << 7)
#define D_ALL 0x00FFU

/** Level meter refresh interval - it animates on its own. */
#define UI_METER_MS 50U

/* Screens ----------------------------------------------------------------- */

typedef enum {
  UI_HOME = 0,
  UI_NOW,
  UI_LIST,
  UI_SETTINGS,
  UI_STATS,
} ui_screen_t;

/** Home tiles, in screen order. */
enum { TILE_MUSIC = 0, TILE_LIST, TILE_STATS, TILE_SETTINGS };

/** Settings rows, in screen order. */
enum {
  SET_THEME = 0,
  SET_BRIGHTNESS,
  SET_BLANK,
  SET_FADE,
  SET_SHUFFLE,
  SET_REPEAT,
  SET_COUNT,
};

/** Read-only rows on the stats screen. */
enum {
  STAT_LISTENING = 0,
  STAT_TRACKS,
  STAT_SESSIONS,
  STAT_SUPPLY,
  STAT_BATTERY,
  STAT_CYCLES,
  STAT_COUNT,
};

#define MENU_LABEL_MAX 48
#define MENU_VALUE_MAX 12

/** Backlight steps the brightness row cycles through. */
static const uint8_t brightness_steps[] = {20U, 40U, 60U, 80U, 100U};
#define BRIGHTNESS_STEPS                                                       \
  ((uint8_t)(sizeof(brightness_steps) / sizeof(brightness_steps[0])))

/** Screen-off delays. Index 0 never blanks. */
static const uint16_t blank_seconds[] = {0U, 15U, 30U, 60U, 300U};
static const char *const blank_labels[] = {"NEVER", "15 S", "30 S", "1 MIN",
                                           "5 MIN"};
#define BLANK_STEPS                                                            \
  ((uint8_t)(sizeof(blank_seconds) / sizeof(blank_seconds[0])))

/*
 * Backlight fade speed, as the step taken every BL_STEP_MS. 100 covers the
 * whole range in one step, which is the "no fade" case; 1 takes a second.
 */
static const uint8_t fade_steps[] = {100U, 8U, 3U, 1U};
static const char *const fade_labels[] = {"INSTANT", "FAST", "NORMAL", "SLOW"};
#define FADE_STEPS ((uint8_t)(sizeof(fade_steps) / sizeof(fade_steps[0])))

/**
 * @brief A scrollable menu: rows that fill in their own text.
 *
 * The track list and the settings page are the same shape, so they share one
 * painter. There is no menu header - the status bar already names the screen.
 */
typedef struct {
  uint8_t (*count)(void);
  void (*row)(uint8_t entry, char *label, char *value, uint16_t *color);
} menu_def_t;

static ui_screen_t screen;
static uint8_t home_sel;
static uint8_t list_sel, list_top;
static uint8_t set_sel, set_top;
static uint8_t stat_sel, stat_top;
/* Animation ---------------------------------------------------------------- */

/*
 * Time-based, not frame-based: every tick renders one frame at whatever
 * progress the clock reports, so a slow frame shortens the animation rather
 * than stretching it. Durations are short on purpose - long enough to read as
 * movement and show where a screen came from, short enough that nobody waits
 * on them.
 */
#define ANIM_SCREEN_MS 240U
#define ANIM_SELECT_MS 160U

/** Ease-out cubic over 0..1000. Motion decelerates into place. */
static uint16_t ease_out(uint32_t elapsed, uint16_t duration) {
  uint32_t inv;

  if (elapsed >= duration) {
    return 1000U;
  }
  inv = 1000U - ((elapsed * 1000U) / duration);
  return (uint16_t)(1000U - ((inv * inv * inv) / 1000000U));
}

/** Interpolate @p a to @p b by @p p thousandths. */
static int16_t lerp(int16_t a, int16_t b, uint16_t p) {
  return (int16_t)(a + (((int32_t)(b - a) * (int32_t)p) / 1000));
}

/** Screen push / pop. dir is +1 when the new screen arrives from the right. */
static struct {
  bool active;
  uint32_t start;
  ui_screen_t from;
  int8_t dir;
} slide;

/** Sliding highlight on the menus and sliding focus ring on the home screen. */
static int16_t sel_y_from, sel_y_to;
static int16_t focus_x_from, focus_x_to;
static uint32_t move_start;
/** Repaint the content area until this tick - covers the two above. */
static uint32_t page_anim_until;

/** False until the first screen is on the panel; suppresses the opening slide. */
static bool ui_ready;

static bool bar_dirty;
static bool page_dirty;
static uint16_t pending;
static uint32_t meter_at;

/* Screen blanking --------------------------------------------------------- */

static uint32_t last_input_at;
static bool asleep;

/*
 * The backlight is ramped rather than switched, because a panel that snaps to
 * black reads as a fault. Stepping from the main loop instead of sleeping in a
 * loop means a button press can interrupt a fade half way through.
 */
#define BL_STEP_MS 10U

static uint8_t bl_level;
static uint8_t bl_target;
static uint32_t bl_at;

static void backlight_to(uint8_t target) { bl_target = target; }

static void backlight_now(uint8_t level) {
  bl_level = level;
  bl_target = level;
  st7789_backlight(level);
}

static void backlight_tick(uint32_t now) {
  if (bl_level == bl_target || now - bl_at < BL_STEP_MS) {
    return;
  }
  bl_at = now;

  {
    uint8_t step = fade_steps[settings.fade];

    if (bl_target > bl_level) {
      bl_level =
          (uint8_t)((bl_target - bl_level < step) ? bl_target : bl_level + step);
    } else {
      bl_level =
          (uint8_t)((bl_level - bl_target < step) ? bl_target : bl_level - step);
    }
  }
  st7789_backlight(bl_level);
}

static marquee_t title_mq;
static marquee_t artist_mq;

/** Last state the screen was drawn from. */
static struct {
  ui_screen_t screen;
  uint8_t index;
  uint16_t elapsed_s;
  uint8_t volume;
  uint8_t battery;
  bool playing;
  bool shuffle;
  bool repeat;
} shown;

/* Small formatters - avoids dragging newlib's printf in for a few strings. */

static char *put_u16(char *p, uint16_t v, uint8_t min_digits) {
  char tmp[6];
  uint8_t n = 0;

  do {
    tmp[n++] = (char)('0' + (v % 10U));
    v /= 10U;
  } while (v);
  while (n < min_digits) {
    tmp[n++] = '0';
  }
  while (n) {
    *p++ = tmp[--n];
  }
  return p;
}

/** "3:05" / "12:05" */
static void fmt_mmss(char *out, uint16_t secs) {
  char *p = put_u16(out, (uint16_t)(secs / 60U), 1);
  *p++ = ':';
  p = put_u16(p, (uint16_t)(secs % 60U), 2);
  *p = '\0';
}

static int16_t center_in(int16_t x, int16_t w, const gfx_font_t *f,
                         const char *s) {
  return (int16_t)(x + (w - gfx_text_w(f, s)) / 2);
}

static int16_t right_to(int16_t right, const gfx_font_t *f, const char *s) {
  return (int16_t)(right - gfx_text_w(f, s));
}

/* Shared glyphs ----------------------------------------------------------- */

/**
 * Two paths that run in straight, cross, and leave through arrowheads.
 *
 * The straight stubs at both ends are the whole trick: a bare X reads as a
 * cross or a close button, and only the entries and exits make it read as two
 * routes swapping over.
 */
static void draw_shuffle(int16_t x, int16_t y, int16_t w, int16_t h,
                         uint16_t c) {
  int16_t top = (int16_t)(y + 1);
  int16_t bot = (int16_t)(y + h - 2);
  int16_t xa = (int16_t)(x + 2);
  int16_t xb = (int16_t)(x + w - 5);

  gfx_hline(x, top, 3, c);
  gfx_hline(x, bot, 3, c);
  gfx_line(xa, top, xb, bot, c);
  gfx_line(xa, bot, xb, top, c);
  gfx_hline(xb, top, 2, c);
  gfx_hline(xb, bot, 2, c);
  gfx_tri((int16_t)(x + w - 4), (int16_t)(top - 1), 4, 3, GFX_TRI_RIGHT, c);
  gfx_tri((int16_t)(x + w - 4), (int16_t)(bot - 1), 4, 3, GFX_TRI_RIGHT, c);
}

/**
 * A cycle: out along the top, back along the bottom, with the short verticals
 * closing the loop so it does not read as two unrelated arrows.
 */
static void draw_repeat(int16_t x, int16_t y, int16_t w, int16_t h,
                        uint16_t c) {
  int16_t top = (int16_t)(y + 1);
  int16_t bot = (int16_t)(y + h - 2);

  gfx_hline((int16_t)(x + 1), top, (int16_t)(w - 5), c);
  gfx_tri((int16_t)(x + w - 4), (int16_t)(top - 1), 4, 3, GFX_TRI_RIGHT, c);
  gfx_vline(x, top, 3, c);

  gfx_hline((int16_t)(x + 4), bot, (int16_t)(w - 5), c);
  gfx_tri(x, (int16_t)(bot - 1), 4, 3, GFX_TRI_LEFT, c);
  gfx_vline((int16_t)(x + w - 1), (int16_t)(bot - 2), 3, c);
}

/**
 * Battery pill: 16x9 body plus a 2x3 terminal, 18 px overall.
 *
 * Drawn as an outline rather than a filled-then-inset rect, so it does not
 * assume the background behind it, and the level is inset far enough to leave
 * a gap all the way round instead of touching the case.
 */
static void draw_battery(int16_t x, int16_t y, uint8_t pct) {
  uint16_t c = (pct <= 15U) ? COL_RED : ((pct <= 35U) ? COL_AMBER : COL_GREEN);
  int16_t fill = (int16_t)(((int32_t)12 * pct + 50) / 100);

  gfx_rrect_ring(x, y, 16, 9, 3, 1, COL_TEXT_MUTE);
  gfx_rrect((int16_t)(x + 16), (int16_t)(y + 3), 2, 3, 1, COL_TEXT_MUTE);
  if (fill > 0) {
    gfx_rrect((int16_t)(x + 2), (int16_t)(y + 2), fill, 5, 1, c);
  }
}

/* Status bar -------------------------------------------------------------- */

/** Screen name for the middle of the bar. */
static void bar_title(char *out) {
  char *p;

  switch (screen) {
  case UI_NOW:
    strcpy(out, "NOW PLAYING ");
    p = out + 12;
    p = put_u16(p, (uint16_t)(player.index + 1U), 1);
    *p++ = '/';
    p = put_u16(p, player.count, 1);
    *p = '\0';
    return;
  case UI_LIST:
    strcpy(out, "TRACKS");
    return;
  case UI_SETTINGS:
    strcpy(out, "SETTINGS");
    return;
  case UI_STATS:
    strcpy(out, "STATS");
    return;
  default:
    strcpy(out, "WALTZ");
    return;
  }
}

static void paint_bar(void *ud) {
  char buf[16];
  char vol[6];
  char *p;

  (void)ud;
  /* The bar sits on the background with a hairline under it rather than on a
   * filled block: chrome should recede and let the content read first. */
  gfx_fill(0, 0, GFX_W, BAR_H, COL_BG);
  gfx_hline(0, BAR_H - 1, GFX_W, gfx_mix(COL_BG, COL_TEXT_MUTE, 70));

  /* Playback state, visible from every screen. */
  if (player.playing) {
    gfx_fill(BAR_STATE_X, 2, 2, 8, COL_ACCENT);
    gfx_fill((int16_t)(BAR_STATE_X + 4), 2, 2, 8, COL_ACCENT);
  } else {
    gfx_tri(BAR_STATE_X, 2, 6, 8, GFX_TRI_RIGHT, COL_TEXT_MUTE);
  }

  draw_shuffle(BAR_SHUFFLE_X, 2, 11, 9,
               player.shuffle ? COL_ACCENT3 : COL_TEXT_MUTE);
  draw_repeat(BAR_REPEAT_X, 2, 11, 9,
              player.repeat ? COL_ACCENT3 : COL_TEXT_MUTE);

  bar_title(buf);
  gfx_text(center_in(0, GFX_W, &Font_Mono6x8, buf), 2, &Font_Mono6x8, buf,
           COL_TEXT_DIM);

  gfx_bitmap(BAR_SPEAKER_X, 1, 10, 9, icon_speaker, COL_TEXT_DIM);
  p = put_u16(vol, player.volume, 1);
  *p++ = '%';
  *p = '\0';
  gfx_text(right_to(BAR_VOL_RIGHT, &Font_Mono6x8, vol), 2, &Font_Mono6x8, vol,
           COL_TEXT_DIM);

  draw_battery(BAR_BATT_X, 1, player.battery);
}

/* Home screen ------------------------------------------------------------- */

static int16_t home_tile_x(uint8_t i) {
  return (int16_t)(HOME_TILE_X0 + i * (HOME_TILE_W + HOME_TILE_GAP));
}

static void paint_home(void *ud) {
  static const char *const label[HOME_TILES] = {"MUSIC", "TRACKS", "STATS",
                                                "SETTINGS"};
  static const int16_t knob[3] = {4, 14, 9};
  static const uint8_t chart[3] = {8U, 16U, 12U};
  uint16_t p = ease_out(HAL_GetTick() - move_start, ANIM_SELECT_MS);
  int16_t fx = lerp(focus_x_from, focus_x_to, p);
  uint8_t i;

  (void)ud;
  gfx_fill(0, CONTENT_Y, GFX_W, CONTENT_H, COL_BG);

  /* Plates first, then the focus fill over the one it is on, then all the tile
   * content, then the ring. Anything drawn after the content would wipe it. */
  for (i = 0; i < HOME_TILES; ++i) {
    gfx_rrect(home_tile_x(i), HOME_TILE_Y, HOME_TILE_W, HOME_TILE_H, 12,
              gfx_mix(COL_BG, COL_CARD, 200));
  }
  gfx_rrect(fx, HOME_TILE_Y, HOME_TILE_W, HOME_TILE_H, 12,
            gfx_mix(COL_BG, COL_ACCENT, 45));

  for (i = 0; i < HOME_TILES; ++i) {
    int16_t x = home_tile_x(i);
    int16_t cx = (int16_t)(x + HOME_TILE_W / 2);
    bool on = (i == home_sel);
    uint16_t ink = on ? COL_TEXT : COL_TEXT_DIM;
    uint16_t mark = on ? COL_ACCENT : COL_TEXT_MUTE;
    uint8_t k;

    switch (i) {
    case TILE_MUSIC:
      gfx_disc(cx, 34, 10, GFX_RGB(0x0E, 0x0E, 0x14));
      gfx_ring(cx, 34, 10, 9, mark);
      gfx_disc(cx, 34, 3, mark);
      break;
    case TILE_LIST:
      for (k = 0; k < 3U; ++k) {
        int16_t ry = (int16_t)(27 + k * 7);
        gfx_fill((int16_t)(cx - 12), ry, 3, 3, mark);
        gfx_fill((int16_t)(cx - 6), ry, 18, 3, ink);
      }
      break;
    case TILE_STATS:
      for (k = 0; k < 3U; ++k) {
        int16_t bh = (int16_t)chart[k];
        gfx_fill((int16_t)(cx - 11 + k * 8), (int16_t)(42 - bh), 6, bh,
                 (k == 2U) ? mark : ink);
      }
      gfx_hline((int16_t)(cx - 12), 43, 25, ink);
      break;
    default:
      for (k = 0; k < 3U; ++k) {
        int16_t ry = (int16_t)(28 + k * 6);
        gfx_fill((int16_t)(cx - 12), ry, 24, 2, ink);
        gfx_fill((int16_t)(cx - 12 + knob[k]), (int16_t)(ry - 2), 4, 6, mark);
      }
      break;
    }

    gfx_text(center_in(x, HOME_TILE_W, &Font_Mono6x8, label[i]), 50,
             &Font_Mono6x8, label[i], ink);
  }

  gfx_rrect_ring(fx, HOME_TILE_Y, HOME_TILE_W, HOME_TILE_H, 12, 1, COL_ACCENT);
}

/* Marquee ----------------------------------------------------------------- */

static void paint_marquee(void *ud) {
  marquee_t *m = (marquee_t *)ud;
  int16_t ty = (int16_t)(m->y + (m->h - m->font->height) / 2);
  uint16_t fg = m->dim ? COL_TEXT_DIM : COL_TEXT;

  gfx_fill(m->x, m->y, m->w, m->h, COL_BG);

  /* Explicit, because this is also called from the whole-screen repaint where
   * the flush region is the entire panel and would clip nothing. */
  gfx_clip(m->x, m->y, m->w, m->h);
  if (m->scrolling) {
    gfx_text((int16_t)(m->x - m->offset), ty, m->font, m->text, fg);
    gfx_text((int16_t)(m->x - m->offset + m->text_w + MARQUEE_GAP), ty, m->font,
             m->text, fg);
  } else {
    gfx_text(m->x, ty, m->font, m->text, fg);
  }
  gfx_clip_reset();
}

static void marquee_set(marquee_t *m, const char *text, uint32_t now) {
  strncpy(m->text, text, MARQUEE_BUF - 1U);
  m->text[MARQUEE_BUF - 1U] = '\0';
  m->text_w = gfx_text_w(m->font, m->text);
  m->offset = 0;
  m->scrolling = (m->text_w > m->w);
  m->step_at = now;
  m->hold_until = now + MARQUEE_HOLD_MS;
  m->dirty = true;
}

static void marquee_tick(marquee_t *m, uint32_t now) {
  if (!m->scrolling || (int32_t)(now - m->hold_until) < 0) {
    return;
  }
  if (now - m->step_at < MARQUEE_MS) {
    return;
  }
  m->step_at = now;
  m->offset++;
  if (m->offset >= (int16_t)(m->text_w + MARQUEE_GAP)) {
    m->offset = 0;
    m->hold_until = now + MARQUEE_HOLD_MS;
  }
  m->dirty = true;
}

static void marquee_render(marquee_t *m) {
  if (!m->dirty) {
    return;
  }
  gfx_flush(m->x, m->y, m->w, m->h, paint_marquee, m);
  m->dirty = false;
}

/* Player screen ----------------------------------------------------------- */

static void paint_art(void *ud) {
  const track_t *t = Player_Track();
  const int16_t cx = (int16_t)(ART_X + ART_SIZE / 2);
  const int16_t cy = (int16_t)(ART_Y + ART_SIZE / 2);
  int16_t r;

  (void)ud;
  gfx_fill(0, CONTENT_Y, ART_ZONE_W, CONTENT_H, COL_BG);

  gfx_rrect_grad(ART_X, ART_Y, ART_SIZE, ART_SIZE, 10, t->art_top,
                 t->art_bottom);

  gfx_disc(cx, cy, 21, GFX_RGB(0x0E, 0x0E, 0x14));
  gfx_ring(cx, cy, 21, 20, gfx_mix(t->art_top, COL_TEXT, 90));
  for (r = 18; r >= 11; r = (int16_t)(r - 3)) {
    gfx_ring(cx, cy, r, (int16_t)(r - 1), GFX_RGB(0x24, 0x24, 0x30));
  }

  gfx_disc(cx, cy, 8, t->art_label);
  gfx_bitmap((int16_t)(cx - 3), (int16_t)(cy - 4), 7, 9, icon_note,
             GFX_RGB(0x14, 0x10, 0x18));
}

static void paint_bar_row(void *ud) {
  const track_t *t = Player_Track();
  const int16_t ty = ROW_BAR_Y + (ROW_BAR_H - 3) / 2;
  int16_t fw;

  (void)ud;
  gfx_fill(MID_X, ROW_BAR_Y, MID_W, ROW_BAR_H, COL_BG);

  gfx_rrect(MID_X, ty, MID_W, 3, 1, gfx_mix(COL_BG, COL_CARD_HI, 220));

  fw = t->duration_s
           ? (int16_t)(((int32_t)MID_W * player.elapsed_s) / t->duration_s)
           : 0;
  if (fw > MID_W) {
    fw = MID_W;
  }
  if (fw > 0) {
    gfx_rrect(MID_X, ty, fw, 3, 1, COL_ACCENT);
  }

  /* A small dot rather than a grabbable knob - nothing here is draggable. */
  gfx_disc((int16_t)(MID_X + fw), (int16_t)(ty + 1), 2, COL_ACCENT);
}

static void paint_time(void *ud) {
  const track_t *t = Player_Track();
  char left[8];
  char right[8];

  (void)ud;
  gfx_fill(MID_X, ROW_TIME_Y, MID_W, ROW_TIME_H, COL_BG);

  fmt_mmss(left, player.elapsed_s);
  fmt_mmss(right, t->duration_s);

  gfx_text(MID_X, (int16_t)(ROW_TIME_Y + 1), &Font_Mono6x8, left, COL_TEXT);
  gfx_text(right_to((int16_t)(MID_X + MID_W), &Font_Mono6x8, right),
           (int16_t)(ROW_TIME_Y + 1), &Font_Mono6x8, right, COL_TEXT_MUTE);
}

static void paint_format(void *ud) {
  const track_t *t = Player_Track();
  char buf[10];
  char *p;

  (void)ud;
  gfx_fill(MID_X, ROW_FORMAT_Y, MID_W, ROW_FORMAT_H, COL_BG);

  p = put_u16(buf, t->bitrate_kbps, 1);
  strcpy(p, " kbps");
  gfx_text(MID_X, (int16_t)(ROW_FORMAT_Y + 1), &Font_Mono6x8, buf,
           COL_TEXT_MUTE);
}

static void paint_transport(void *ud) {
  const int16_t cy = ROW_TRANSPORT_Y + ROW_TRANSPORT_H / 2;

  (void)ud;
  gfx_fill(RIGHT_X, ROW_TRANSPORT_Y, RIGHT_W, ROW_TRANSPORT_H, COL_BG);

  /* The end bars are what say "previous track" rather than "rewind". */
  gfx_fill(208, (int16_t)(cy - 5), 2, 10, COL_TEXT_DIM);
  gfx_tri(211, (int16_t)(cy - 5), 5, 10, GFX_TRI_LEFT, COL_TEXT_DIM);
  gfx_tri(217, (int16_t)(cy - 5), 5, 10, GFX_TRI_LEFT, COL_TEXT_DIM);

  /* No disc behind it: the glyph carries the accent, and being a size larger
   * than its neighbours is enough to make it the focal point. */
  if (player.playing) {
    gfx_fill(233, (int16_t)(cy - 7), 3, 14, COL_ACCENT);
    gfx_fill(239, (int16_t)(cy - 7), 3, 14, COL_ACCENT);
  } else {
    gfx_tri(232, (int16_t)(cy - 7), 11, 14, GFX_TRI_RIGHT, COL_ACCENT);
  }

  gfx_tri(255, (int16_t)(cy - 5), 5, 10, GFX_TRI_RIGHT, COL_TEXT_DIM);
  gfx_tri(261, (int16_t)(cy - 5), 5, 10, GFX_TRI_RIGHT, COL_TEXT_DIM);
  gfx_fill(267, (int16_t)(cy - 5), 2, 10, COL_TEXT_DIM);
}

static void paint_meter(void *ud) {
  const int16_t base = ROW_METER_Y + ROW_METER_H - 4;
  /* Mixed from the background towards the card rather than dimmed towards
   * black: dimming stays subtle on a dark theme but turns the groove into solid
   * bars on a light one. */
  const uint16_t groove = gfx_mix(COL_BG, COL_CARD, 180);
  uint8_t i;

  (void)ud;
  gfx_fill(RIGHT_X, ROW_METER_Y, RIGHT_W, ROW_METER_H, COL_BG);

  for (i = 0; i < SPECTRUM_BARS; ++i) {
    int16_t bx = (int16_t)(METER_X + i * (METER_BAR_W + METER_GAP));
    int16_t hb = (int16_t)(((int32_t)METER_MAX_H * player.level[i]) / 100);
    int16_t hp = (int16_t)(((int32_t)METER_MAX_H * player.peak[i]) / 100);

    gfx_fill(bx, (int16_t)(base - METER_MAX_H), METER_BAR_W, METER_MAX_H,
             groove);

    if (hb > 0) {
      uint16_t tip = gfx_mix(COL_ACCENT, COL_AMBER,
                             (uint8_t)(((int32_t)hb * 255) / METER_MAX_H));
      gfx_vgrad(bx, (int16_t)(base - hb), METER_BAR_W, hb, tip, COL_ACCENT2);
    }
    if (hp > 1) {
      gfx_fill(bx, (int16_t)(base - hp - 1), METER_BAR_W, 1, COL_TEXT_DIM);
    }
  }

  gfx_hline(METER_X, (int16_t)(base + 1),
            SPECTRUM_BARS * (METER_BAR_W + METER_GAP) - METER_GAP, COL_CARD);
}

/* Menus ------------------------------------------------------------------- */

static uint8_t list_count(void) { return player.count; }

static uint8_t settings_count(void) { return (uint8_t)SET_COUNT; }

static void list_row(uint8_t entry, char *label, char *value,
                     uint16_t *color) {
  char *p = put_u16(label, (uint16_t)(entry + 1U), 2);

  *p++ = ' ';
  strncpy(p, Player_TrackAt(entry)->title, MENU_LABEL_MAX - 5U);
  p[MENU_LABEL_MAX - 5U] = '\0';

  fmt_mmss(value, Player_TrackAt(entry)->duration_s);
  *color = (entry == player.index) ? COL_ACCENT : COL_TEXT_DIM;
}

static void settings_row(uint8_t entry, char *label, char *value,
                         uint16_t *color) {
  char *p;

  *color = COL_TEXT_DIM;
  value[0] = '\0';

  switch (entry) {
  case SET_THEME:
    strcpy(label, "THEME");
    strcpy(value, Theme_Name(Theme_Index()));
    *color = COL_ACCENT;
    break;
  case SET_BRIGHTNESS:
    strcpy(label, "BRIGHTNESS");
    p = put_u16(value, brightness_steps[settings.brightness], 1);
    *p++ = '%';
    *p = '\0';
    break;
  case SET_BLANK:
    strcpy(label, "SCREEN OFF");
    strcpy(value, blank_labels[settings.blank]);
    break;
  case SET_FADE:
    strcpy(label, "FADE");
    strcpy(value, fade_labels[settings.fade]);
    break;
  case SET_SHUFFLE:
    strcpy(label, "SHUFFLE");
    strcpy(value, player.shuffle ? "ON" : "OFF");
    if (player.shuffle) {
      *color = COL_ACCENT3;
    }
    break;
  default:
    strcpy(label, "REPEAT");
    strcpy(value, player.repeat ? "ON" : "OFF");
    if (player.repeat) {
      *color = COL_ACCENT3;
    }
    break;
  }
}

static uint8_t stats_count(void) { return (uint8_t)STAT_COUNT; }

static void stats_row(uint8_t entry, char *label, char *value,
                      uint16_t *color) {
  char *p;

  *color = COL_TEXT_DIM;
  value[0] = '\0';

  switch (entry) {
  case STAT_LISTENING:
    strcpy(label, "LISTENING");
    p = put_u16(value, (uint16_t)(stats.listen_s / 3600U), 1);
    *p++ = 'h';
    *p++ = ' ';
    p = put_u16(p, (uint16_t)((stats.listen_s / 60U) % 60U), 2);
    *p++ = 'm';
    *p = '\0';
    *color = COL_ACCENT;
    break;
  case STAT_TRACKS:
    strcpy(label, "TRACKS PLAYED");
    p = put_u16(value, (uint16_t)stats.tracks, 1);
    *p = '\0';
    break;
  case STAT_SESSIONS:
    strcpy(label, "POWER ONS");
    p = put_u16(value, stats.sessions, 1);
    *p = '\0';
    break;
  case STAT_SUPPLY: {
    uint16_t mv = Power_SupplyMv();
    strcpy(label, "SUPPLY");
    if (mv == 0U) {
      strcpy(value, "---");
    } else {
      p = put_u16(value, (uint16_t)(mv / 1000U), 1);
      *p++ = '.';
      p = put_u16(p, (uint16_t)((mv / 10U) % 100U), 2);
      *p++ = ' ';
      *p++ = 'V';
      *p = '\0';
    }
    break;
  }
  case STAT_BATTERY:
    strcpy(label, "BATTERY");
    /* No cell and, on this package, no ADC pin left to sense one - see
     * power.h. Saying so beats inventing a percentage. */
    strcpy(value, Power_HasBatterySense() ? "OK" : "NO SENSE");
    *color = COL_TEXT_MUTE;
    break;
  default:
    strcpy(label, "CHARGE CYCLES");
    if (Power_HasBatterySense()) {
      p = put_u16(value, stats.cycles, 1);
      *p = '\0';
    } else {
      strcpy(value, "-");
    }
    *color = COL_TEXT_MUTE;
    break;
  }
}

static const menu_def_t menu_stats = {stats_count, stats_row};
static const menu_def_t menu_list = {list_count, list_row};
static const menu_def_t menu_settings = {settings_count, settings_row};

static const menu_def_t *active_menu(void) {
  if (screen == UI_SETTINGS) {
    return &menu_settings;
  }
  return (screen == UI_STATS) ? &menu_stats : &menu_list;
}

static uint8_t *active_sel(void) {
  if (screen == UI_SETTINGS) {
    return &set_sel;
  }
  return (screen == UI_STATS) ? &stat_sel : &list_sel;
}

static uint8_t *active_top(void) {
  if (screen == UI_SETTINGS) {
    return &set_top;
  }
  return (screen == UI_STATS) ? &stat_top : &list_top;
}

static void paint_menu(void *ud) {
  const menu_def_t *m = (const menu_def_t *)ud;
  const uint8_t sel = *active_sel();
  const uint8_t top = *active_top();
  char label[MENU_LABEL_MAX];
  char value[MENU_VALUE_MAX];
  uint8_t row;

  gfx_fill(0, CONTENT_Y, GFX_W, CONTENT_H, COL_BG);

  {
    /* Highlight first, then the rows on top of it, so it reads as a surface
     * the text sits on rather than a box drawn around the text. */
    uint16_t p = ease_out(HAL_GetTick() - move_start, ANIM_SELECT_MS);
    int16_t hy = lerp(sel_y_from, sel_y_to, p);

    gfx_rrect(3, hy, (int16_t)(GFX_W - 6), MENU_ROW_H, 4,
              gfx_mix(COL_BG, COL_ACCENT, 45));
  }

  for (row = 0; row < MENU_ROWS; ++row) {
    uint8_t entry = (uint8_t)(top + row);
    int16_t y = (int16_t)(MENU_Y + row * MENU_ROW_H);
    uint16_t color = COL_TEXT_DIM;

    if (entry >= m->count()) {
      break;
    }

    m->row(entry, label, value, &color);

    if (entry == sel) {
      if (color == COL_TEXT_DIM) {
        color = COL_TEXT;
      }
    }

    gfx_text(6, (int16_t)(y + 2), &Font_Mono6x8, label, color);
    if (value[0]) {
      gfx_text(right_to((int16_t)(GFX_W - 6), &Font_Mono6x8, value),
               (int16_t)(y + 2), &Font_Mono6x8, value, color);
    }
  }
}

static void menu_scroll_into_view(void) {
  uint8_t count = active_menu()->count();
  uint8_t *sel = active_sel();
  uint8_t *top = active_top();

  if (*sel < *top) {
    *top = *sel;
  } else if (*sel >= (uint8_t)(*top + MENU_ROWS)) {
    *top = (uint8_t)(*sel - MENU_ROWS + 1U);
  }
  /* A wrap can leave the window past the end of a short list. */
  if (count > MENU_ROWS && *top > (uint8_t)(count - MENU_ROWS)) {
    *top = (uint8_t)(count - MENU_ROWS);
  } else if (count <= MENU_ROWS) {
    *top = 0;
  }
}

/** Menus wrap: past the last row is the first one again. */
/** Pixel row the highlight sits on for the current selection. */
static int16_t menu_sel_y(void) {
  return (int16_t)(MENU_Y + (*active_sel() - *active_top()) * MENU_ROW_H);
}

static void menu_move(int8_t delta) {
  uint8_t count = active_menu()->count();
  int16_t next = (int16_t)((int16_t)*active_sel() + delta);
  uint8_t top_before;

  if (next < 0) {
    next = (int16_t)(count - 1U);
  } else if (next >= (int16_t)count) {
    next = 0;
  }
  sel_y_from = menu_sel_y();
  top_before = *active_top();
  *active_sel() = (uint8_t)next;
  menu_scroll_into_view();
  sel_y_to = menu_sel_y();

  /* When the list scrolls the rows move under the highlight, so sliding it as
   * well reads as two things moving at once. Snap instead. */
  if (*active_top() != top_before) {
    sel_y_from = sel_y_to;
  }
  move_start = HAL_GetTick();
  page_anim_until = move_start + ANIM_SELECT_MS;
  page_dirty = true;
}

/* Panel check -------------------------------------------------------------- */

#define PANEL_CHECK_STRIPS 6

static void paint_panel_check(void *ud) {
  static const char *const label[PANEL_CHECK_STRIPS] = {
      "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};
  const uint16_t strip[PANEL_CHECK_STRIPS] = {
      GFX_RGB(0xFF, 0x00, 0x00), GFX_RGB(0x00, 0xFF, 0x00),
      GFX_RGB(0x00, 0x00, 0xFF), GFX_RGB(0xFF, 0xFF, 0x00),
      GFX_RGB(0x00, 0xFF, 0xFF), GFX_RGB(0xFF, 0x00, 0xFF)};
  const int16_t w = GFX_W / PANEL_CHECK_STRIPS;
  const uint16_t black = 0x0000U;
  uint8_t i;

  (void)ud;
  for (i = 0; i < PANEL_CHECK_STRIPS; ++i) {
    int16_t x = (int16_t)(i * w);
    int16_t cw = (i == PANEL_CHECK_STRIPS - 1U) ? (int16_t)(GFX_W - x) : w;
    gfx_fill(x, 0, cw, GFX_H, strip[i]);
    gfx_text(center_in(x, cw, &Font_Mono6x8, label[i]),
             (int16_t)(GFX_H / 2 - 4), &Font_Mono6x8, label[i], black);
  }
}

void Ui_PanelCheck(void) {
  gfx_flush(0, 0, GFX_W, GFX_H, paint_panel_check, NULL);
  st7789_backlight(100);
  HAL_Delay(15000);
  st7789_backlight(0);
}

/* Colour-format sweep ------------------------------------------------------ */

#define SWEEP_COUNT 8
#define SWEEP_HEAD_H 22

typedef struct {
  uint8_t index;
  bool swap_bytes;
  bool bgr;
  bool invert;
} sweep_cfg_t;

static uint16_t sweep_word(uint16_t rgb565, bool swap_bytes) {
  if (swap_bytes) {
    return rgb565;
  }
  return (uint16_t)(((rgb565 & 0x00FFU) << 8) | (rgb565 >> 8));
}

static void paint_sweep(void *ud) {
  static const char *const label[3] = {"RED", "GREEN", "BLUE"};
  static const uint16_t plain[3] = {0xF800U, 0x07E0U, 0x001FU};
  const sweep_cfg_t *cfg = (const sweep_cfg_t *)ud;
  const int16_t strip_w = GFX_W / 3;
  const uint16_t black = 0x0000U;
  char flags[10];
  char digit[2];
  uint8_t i;

  gfx_fill(0, 0, GFX_W, SWEEP_HEAD_H, black);

  digit[0] = (char)('0' + cfg->index);
  digit[1] = '\0';
  gfx_text(6, 2, &Font_Mono11x18, digit, 0xFFFFU);

  flags[0] = 'S';
  flags[1] = (char)('0' + (cfg->swap_bytes ? 1 : 0));
  flags[2] = ' ';
  flags[3] = 'B';
  flags[4] = (char)('0' + (cfg->bgr ? 1 : 0));
  flags[5] = ' ';
  flags[6] = 'I';
  flags[7] = (char)('0' + (cfg->invert ? 1 : 0));
  flags[8] = '\0';
  gfx_text(24, 7, &Font_Mono6x8, flags, 0xFFFFU);

  for (i = 0; i < 3U; ++i) {
    int16_t x = (int16_t)(i * strip_w);
    int16_t w = (i == 2U) ? (int16_t)(GFX_W - x) : strip_w;
    gfx_fill(x, SWEEP_HEAD_H, w, (int16_t)(GFX_H - SWEEP_HEAD_H),
             sweep_word(plain[i], cfg->swap_bytes));
    gfx_text(center_in(x, w, &Font_Mono6x8, label[i]),
             (int16_t)(SWEEP_HEAD_H + (GFX_H - SWEEP_HEAD_H) / 2 - 4),
             &Font_Mono6x8, label[i], black);
  }
}

void Ui_ColorSweep(void) {
  st7789_backlight(100);

  for (;;) {
    uint8_t n;
    for (n = 0; n < SWEEP_COUNT; ++n) {
      sweep_cfg_t cfg;

      cfg.index = (uint8_t)(n + 1U);
      cfg.swap_bytes = ((n & 0x04U) != 0U);
      cfg.bgr = ((n & 0x02U) != 0U);
      cfg.invert = ((n & 0x01U) != 0U);

      st7789_set_color_mode(cfg.bgr, cfg.invert);
      gfx_flush(0, 0, GFX_W, GFX_H, paint_sweep, &cfg);
      HAL_Delay(4000);
    }
  }
}

/* Splash ------------------------------------------------------------------ */

static void paint_splash(void *ud) {
  /* Bar heights for the decorative meter on the right. */
  static const uint8_t deco[8] = {14U, 26U, 20U, 34U, 28U, 18U, 24U, 12U};
  uint8_t i;

  (void)ud;
  gfx_fill(0, 0, GFX_W, GFX_H, COL_BG);

  /* A one-pixel frame around all four edges. It reads as a bezel, and it is
   * still the quickest way to spot a wrong CASET/RASET offset. */
  gfx_hline(0, 0, GFX_W, COL_CARD_HI);
  gfx_hline(0, GFX_H - 1, GFX_W, COL_CARD_HI);
  gfx_vline(0, 0, GFX_H, COL_CARD_HI);
  gfx_vline(GFX_W - 1, 0, GFX_H, COL_CARD_HI);

  /* Record on a sleeve, same motif as the cover art. */
  gfx_rrect_grad(10, 8, 60, 60, 12, COL_ACCENT2, COL_ACCENT);
  gfx_disc(40, 38, 18, GFX_RGB(0x0E, 0x0E, 0x14));
  gfx_ring(40, 38, 18, 17, gfx_mix(COL_ACCENT2, COL_TEXT, 90));
  gfx_disc(40, 38, 8, COL_AMBER);
  gfx_bitmap(37, 34, 7, 9, icon_note, GFX_RGB(0x14, 0x10, 0x18));

  gfx_text(84, 18, &Font_Mono11x18, "Waltz", COL_TEXT);
  gfx_fill(84, 40, 55, 1, COL_ACCENT);
  gfx_text(84, 47, &Font_Mono6x8, "by Rodium Labs", COL_TEXT_DIM);

  for (i = 0; i < 8U; ++i) {
    int16_t x = (int16_t)(200 + i * 10);
    int16_t h = (int16_t)deco[i];
    gfx_vgrad(x, (int16_t)(56 - h), 6, h,
              gfx_mix(COL_ACCENT, COL_AMBER, (uint8_t)(h * 6U)), COL_ACCENT2);
  }
  gfx_hline(200, 57, 76, COL_CARD);
}

void Ui_Splash(void) {
  uint8_t target;
  uint8_t step;
  uint16_t b;

  /* The stored theme has to be live before anything is drawn, or the splash
   * comes up in the defaults and then the first screen snaps to the real
   * palette. */
  Theme_Set(settings.theme);

  gfx_flush(0, 0, GFX_W, GFX_H, paint_splash, NULL);

  /* Ui_Tick() is not running yet, so this ramp is inline - but it uses the same
   * step the FADE setting picks, so the splash comes up at whatever speed the
   * rest of the UI fades at. */
  target = brightness_steps[settings.brightness];
  step = fade_steps[settings.fade];

  for (b = 0; b < target; b = (uint16_t)(b + step)) {
    st7789_backlight((uint8_t)b);
    HAL_Delay(10);
  }
  st7789_backlight(target);
  HAL_Delay(1400);
}

/* Whole-screen painters --------------------------------------------------- */

/*
 * The player is normally drawn as independent dirty regions, but a transition
 * has to put the entire screen down in one pass - including the gaps between
 * widgets, which the per-region paths never touch.
 */
static void paint_player_all(void *ud) {
  (void)ud;
  gfx_fill(0, CONTENT_Y, GFX_W, CONTENT_H, COL_BG);
  paint_art(NULL);
  paint_marquee(&title_mq);
  paint_marquee(&artist_mq);
  paint_bar_row(NULL);
  paint_time(NULL);
  paint_format(NULL);
  paint_transport(NULL);
  paint_meter(NULL);
}

/** Paint any screen's content area. Menus read the active screen, so it is
 *  briefly pointed at the one being drawn. */
static void paint_screen_content(ui_screen_t which) {
  ui_screen_t saved = screen;

  screen = which;
  if (which == UI_HOME) {
    paint_home(NULL);
  } else if (which == UI_NOW) {
    paint_player_all(NULL);
  } else {
    paint_menu((void *)active_menu());
  }
  screen = saved;
}

static void paint_slide(void *ud) {
  uint16_t p = *(const uint16_t *)ud;
  int16_t travel = (int16_t)(GFX_W * slide.dir);

  gfx_translate(lerp(0, (int16_t)-travel, p), 0);
  paint_screen_content(slide.from);
  gfx_translate(lerp(travel, 0, p), 0);
  paint_screen_content(screen);
  gfx_translate(0, 0);
}

/* Navigation -------------------------------------------------------------- */

static uint8_t brightness_percent(void) {
  return brightness_steps[settings.brightness];
}

static void load_track_text(uint32_t now) {
  marquee_set(&title_mq, Player_Track()->title, now);
  marquee_set(&artist_mq, Player_Track()->artist, now);
}

static void go(ui_screen_t next) {
  ui_screen_t prev = screen;

  screen = next;
  bar_dirty = true;
  page_dirty = true;
  pending = D_ALL;
  if (next == UI_NOW) {
    title_mq.dirty = true;
    artist_mq.dirty = true;
  }

  if (!ui_ready) {
    gfx_clear(COL_BG);
    return;
  }

  /* Direction carries the hierarchy: going into something pushes it in from
   * the right, coming back out slides it away to the right. */
  slide.active = true;
  slide.start = HAL_GetTick();
  slide.from = prev;
  slide.dir = (next == UI_HOME) ? -1 : 1;
}

static void enter_home(void) { go(UI_HOME); }

static void enter_now(void) { go(UI_NOW); }

static void enter_list(void) {
  list_sel = player.index;
  list_top = 0;
  go(UI_LIST);
  menu_scroll_into_view();
}

static void enter_settings(void) { go(UI_SETTINGS); }

static void enter_stats(void) { go(UI_STATS); }

/** Leaving settings is the commit point for the stored configuration. */
static void leave_settings(void) {
  settings.theme = Theme_Index();
  (void)Settings_Save();
  enter_home();
}

static void home_activate(void) {
  switch (home_sel) {
  case TILE_MUSIC:
    enter_now();
    break;
  case TILE_LIST:
    enter_list();
    break;
  case TILE_STATS:
    enter_stats();
    break;
  default:
    enter_settings();
    break;
  }
}

static void menu_activate(void) {
  if (screen == UI_LIST) {
    Player_Select(*active_sel());
    enter_now();
    return;
  }
  if (screen == UI_STATS) {
    return; /* read-only */
  }

  switch (set_sel) {
  case SET_THEME:
    Theme_Next();
    break;
  case SET_BRIGHTNESS:
    settings.brightness =
        (uint8_t)((settings.brightness + 1U) % BRIGHTNESS_STEPS);
    backlight_to(brightness_percent());
    break;
  case SET_BLANK:
    settings.blank = (uint8_t)((settings.blank + 1U) % BLANK_STEPS);
    break;
  case SET_FADE:
    settings.fade = (uint8_t)((settings.fade + 1U) % FADE_STEPS);
    /* Show the new speed straight away by dipping and coming back. */
    bl_level = 0;
    backlight_to(brightness_percent());
    break;
  case SET_SHUFFLE:
    Player_ToggleShuffle();
    break;
  default:
    Player_ToggleRepeat();
    break;
  }
  page_dirty = true;
  bar_dirty = true;
}

/* Screen blanking --------------------------------------------------------- */

static void wake(uint32_t now) {
  last_input_at = now;
  if (asleep) {
    asleep = false;
    backlight_to(brightness_percent());
  }
}

static void maybe_blank(uint32_t now) {
  uint16_t secs = blank_seconds[settings.blank];

  if (asleep || secs == 0U) {
    return;
  }
  if (now - last_input_at >= (uint32_t)secs * 1000U) {
    /* Stop drawing straight away and let the last frame fade out. */
    asleep = true;
    backlight_to(0);
  }
}

/* Input ------------------------------------------------------------------- */

static void handle_input(uint32_t now) {
  input_event_t e;

  while ((e = Input_Get()) != INPUT_NONE) {
    if (asleep) {
      /* The press that wakes the screen does nothing else - pressing NEXT to
       * see the time should not skip a track. */
      wake(now);
      continue;
    }
    wake(now);

    /* The mode chord works everywhere - that is the point of it. */
    if (e == INPUT_MODE) {
      Player_CyclePlayMode();
      (void)Settings_Save();
      bar_dirty = true;
      if (screen == UI_SETTINGS) {
        page_dirty = true;
      }
      continue;
    }

    switch (screen) {
    case UI_HOME:
      if (e == INPUT_PREV || e == INPUT_NEXT) {
        focus_x_from = home_tile_x(home_sel);
        home_sel = (e == INPUT_PREV)
                       ? (uint8_t)((home_sel + HOME_TILES - 1U) % HOME_TILES)
                       : (uint8_t)((home_sel + 1U) % HOME_TILES);
        focus_x_to = home_tile_x(home_sel);
        move_start = now;
        page_anim_until = now + ANIM_SELECT_MS;
        page_dirty = true;
      } else if (e == INPUT_PLAY) {
        home_activate();
      } else if (e == INPUT_VOL_DOWN) {
        Player_VolumeStep(-2);
        bar_dirty = true;
      } else if (e == INPUT_VOL_UP) {
        Player_VolumeStep(2);
        bar_dirty = true;
      }
      break;

    case UI_NOW:
      switch (e) {
      case INPUT_PLAY:
        Player_TogglePlay();
        pending |= D_TRANSPORT;
        break;
      case INPUT_MENU:
        enter_home();
        break;
      case INPUT_PREV:
        Player_Prev();
        break;
      case INPUT_NEXT:
        Player_Next();
        break;
      case INPUT_PREV_HOLD:
        Player_Seek(-5);
        break;
      case INPUT_NEXT_HOLD:
        Player_Seek(5);
        break;
      case INPUT_VOL_DOWN:
        Player_VolumeStep(-2);
        bar_dirty = true;
        break;
      case INPUT_VOL_UP:
        Player_VolumeStep(2);
        bar_dirty = true;
        break;
      default:
        break;
      }
      break;

    default: /* UI_LIST, UI_SETTINGS */
      switch (e) {
      case INPUT_PREV:
      case INPUT_PREV_HOLD:
        menu_move(-1);
        break;
      case INPUT_NEXT:
      case INPUT_NEXT_HOLD:
        menu_move(1);
        break;
      case INPUT_PLAY:
        menu_activate();
        break;
      case INPUT_MENU:
        if (screen == UI_SETTINGS) {
          leave_settings();
        } else {
          enter_home();
        }
        break;
      case INPUT_VOL_DOWN:
        Player_VolumeStep(-2);
        bar_dirty = true;
        break;
      case INPUT_VOL_UP:
        Player_VolumeStep(2);
        bar_dirty = true;
        break;
      default:
        break;
      }
      break;
    }
  }
}

/* Public API -------------------------------------------------------------- */

static void latch(void) {
  shown.screen = screen;
  shown.index = player.index;
  shown.elapsed_s = player.elapsed_s;
  shown.volume = player.volume;
  shown.battery = player.battery;
  shown.playing = player.playing;
  shown.shuffle = player.shuffle;
  shown.repeat = player.repeat;
}

void Ui_Init(void) {
  uint32_t now = HAL_GetTick();

  Theme_Set(settings.theme);
  backlight_now(brightness_percent());

  title_mq.x = MID_X;
  title_mq.y = ROW_TITLE_Y;
  title_mq.w = MID_W;
  title_mq.h = ROW_TITLE_H;
  title_mq.font = &Font_Roboto16;
  title_mq.dim = false;

  artist_mq.x = MID_X;
  artist_mq.y = ROW_ARTIST_Y;
  artist_mq.w = MID_W;
  artist_mq.h = ROW_ARTIST_H;
  artist_mq.font = &Font_Mono6x8;
  artist_mq.dim = true;

  load_track_text(now);
  latch();
  meter_at = now;
  home_sel = TILE_MUSIC;
  focus_x_from = home_tile_x(TILE_MUSIC);
  focus_x_to = focus_x_from;
  sel_y_from = MENU_Y;
  sel_y_to = MENU_Y;
  move_start = now;
  page_anim_until = now;
  slide.active = false;
  ui_ready = false;
  list_sel = 0;
  list_top = 0;
  set_sel = 0;
  set_top = 0;
  stat_sel = 0;
  stat_top = 0;
  last_input_at = now;
  asleep = false;

  /* Boot lands on the home screen with nothing playing. */
  enter_home();
  ui_ready = true;
}

void Ui_Tick(uint32_t now) {
  uint16_t dirty;

  handle_input(now);
  maybe_blank(now);
  backlight_tick(now);

  if (asleep) {
    return;
  }

  player.battery = Power_Percent();

  if (shown.battery != player.battery || shown.volume != player.volume ||
      shown.playing != player.playing || shown.shuffle != player.shuffle ||
      shown.repeat != player.repeat || shown.screen != screen ||
      (screen == UI_NOW && shown.index != player.index)) {
    bar_dirty = true;
  }

  if (bar_dirty) {
    gfx_flush(0, 0, GFX_W, BAR_H, paint_bar, NULL);
    bar_dirty = false;
  }

  if (slide.active) {
    uint32_t elapsed = now - slide.start;
    uint16_t p = ease_out(elapsed, ANIM_SCREEN_MS);

    gfx_flush(0, CONTENT_Y, GFX_W, CONTENT_H, paint_slide, &p);
    if (elapsed >= ANIM_SCREEN_MS) {
      slide.active = false;
      page_dirty = true;
      pending = D_ALL;
      title_mq.dirty = true;
      artist_mq.dirty = true;
    }
    latch();
    return;
  }

  /* A selection or focus move is mid-flight; keep the page coming. */
  if ((int32_t)(now - page_anim_until) < 0) {
    page_dirty = true;
  }

  if (screen == UI_HOME) {
    if (page_dirty) {
      gfx_flush(0, CONTENT_Y, GFX_W, CONTENT_H, paint_home, NULL);
      page_dirty = false;
    }
    latch();
    return;
  }

  if (screen != UI_NOW) {
    /* The transport keeps running underneath, so the playing-track highlight
     * has to follow it even while nobody is pressing anything. */
    if (shown.index != player.index) {
      page_dirty = true;
    }
    /* Stats tick over on their own, so repaint on the meter cadence. */
    if (screen == UI_STATS && now - meter_at >= UI_METER_MS * 10U) {
      meter_at = now;
      page_dirty = true;
    }
    if (page_dirty) {
      gfx_flush(0, CONTENT_Y, GFX_W, CONTENT_H, paint_menu,
                (void *)active_menu());
      page_dirty = false;
    }
    latch();
    return;
  }

  dirty = pending;
  pending = 0;

  if (page_dirty) {
    dirty = D_ALL;
    page_dirty = false;
  }

  if (shown.index != player.index) {
    dirty |= D_ART | D_FORMAT | D_BAR | D_TIME;
    load_track_text(now);
  }
  if (shown.elapsed_s != player.elapsed_s) {
    dirty |= D_BAR | D_TIME;
  }
  if (shown.playing != player.playing) {
    dirty |= D_TRANSPORT;
  }
  if (now - meter_at >= UI_METER_MS) {
    meter_at = now;
    dirty |= D_METER;
  }

  if (dirty & D_ART) {
    gfx_flush(0, CONTENT_Y, ART_ZONE_W, CONTENT_H, paint_art, NULL);
  }
  if (dirty & D_TITLE) {
    title_mq.dirty = true;
  }
  if (dirty & D_ARTIST) {
    artist_mq.dirty = true;
  }
  if (dirty & D_BAR) {
    gfx_flush(MID_X, ROW_BAR_Y, MID_W, ROW_BAR_H, paint_bar_row, NULL);
  }
  if (dirty & D_TIME) {
    gfx_flush(MID_X, ROW_TIME_Y, MID_W, ROW_TIME_H, paint_time, NULL);
  }
  if (dirty & D_FORMAT) {
    gfx_flush(MID_X, ROW_FORMAT_Y, MID_W, ROW_FORMAT_H, paint_format, NULL);
  }
  if (dirty & D_TRANSPORT) {
    gfx_flush(RIGHT_X, ROW_TRANSPORT_Y, RIGHT_W, ROW_TRANSPORT_H,
              paint_transport, NULL);
  }
  if (dirty & D_METER) {
    gfx_flush(RIGHT_X, ROW_METER_Y, RIGHT_W, ROW_METER_H, paint_meter, NULL);
  }

  marquee_tick(&title_mq, now);
  marquee_tick(&artist_mq, now);
  marquee_render(&title_mq);
  marquee_render(&artist_mq);

  latch();
}
