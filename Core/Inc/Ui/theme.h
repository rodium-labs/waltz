/**
 * @file    theme.h
 * @brief   Palette and layout for the 284x76 landscape player screen.
 *
 * 284x76 is a letterbox: wide enough for 47 characters of the 6x8 font but only
 * nine rows of them tall. So the screen is three columns rather than the stack
 * a portrait bar wants:
 *
 *   +-------------------------------------------------------+
 *   | shuf rep      NOW PLAYING 3/5       vol      battery  |
 *   +--------+---------------------------+------------------+
 *   |        | title                     | prev play next   |
 *   | cover  | artist                    |                  |
 *   | 64x64  | progress                  | level meter      |
 *   |        | 0:12              4:05    |                  |
 *   |        | 320 kbps                  |                  |
 *   +--------+---------------------------+------------------+
 *
 * Each block is an independent redraw region, so only the ones whose data
 * changed get repainted - that is what keeps the banded renderer cheap.
 *
 * The up-next card from the portrait layout is gone: 76 rows do not have room
 * for it once the transport row is in.
 */

#ifndef __THEME_H__
#define __THEME_H__

#include "gfx.h"
#include "player.h" /* SPECTRUM_BARS sizes the meter geometry below */

/* Palette ----------------------------------------------------------------- */

/**
 * @brief One colour scheme.
 *
 * The palette is a runtime table rather than a set of macros so it can be
 * switched from the settings screen. The COL_* names below still work exactly
 * as before - they just dereference the active theme now - which is why none of
 * the drawing code had to change.
 *
 * Per-track cover gradients are *not* in here: they belong to the track, not
 * the theme, and stay compile-time constants in player.c.
 */
typedef struct {
  const char *name;
  uint16_t bg;
  uint16_t card;
  uint16_t card_hi;
  uint16_t text;
  uint16_t text_dim;
  uint16_t text_mute;
  uint16_t accent;
  uint16_t accent2;
  uint16_t accent3;
  uint16_t amber;
  uint16_t green;
  uint16_t red;
} ui_theme_t;

extern const ui_theme_t *ui_theme;

#define COL_BG (ui_theme->bg)
#define COL_CARD (ui_theme->card)
#define COL_CARD_HI (ui_theme->card_hi)
#define COL_TEXT (ui_theme->text)
#define COL_TEXT_DIM (ui_theme->text_dim)
#define COL_TEXT_MUTE (ui_theme->text_mute)
#define COL_ACCENT (ui_theme->accent)
#define COL_ACCENT2 (ui_theme->accent2)
#define COL_ACCENT3 (ui_theme->accent3)
#define COL_AMBER (ui_theme->amber)
#define COL_GREEN (ui_theme->green)
#define COL_RED (ui_theme->red)

uint8_t Theme_Count(void);
uint8_t Theme_Index(void);
const char *Theme_Name(uint8_t index);
void Theme_Set(uint8_t index);
/** Advance to the next theme, wrapping. */
void Theme_Next(void);

/* Global status bar - drawn on every screen ------------------------------- */

/*
 * 12 rows across the full width, so every screen keeps the clock and the
 * transport flags in the same place. Everything else lives below it.
 *
 *   ||  X ~        NOW PLAYING 3/5        <)) 68%      [==]
 */
#define BAR_H 12
#define BAR_STATE_X 4
#define BAR_SHUFFLE_X 16
#define BAR_REPEAT_X 30
#define BAR_SPEAKER_X 214
#define BAR_VOL_RIGHT 246
#define BAR_BATT_X 258

/** Everything that is not the status bar. */
#define CONTENT_Y BAR_H
#define CONTENT_H (GFX_H - BAR_H)

/* Player screen ----------------------------------------------------------- */

/** Redraw region for the cover, margins included. */
#define ART_ZONE_W 62
#define ART_X 4
#define ART_Y 16
#define ART_SIZE 56

#define MID_X 64
#define MID_W 124

#define ROW_TITLE_Y 14
#define ROW_TITLE_H 18
#define ROW_ARTIST_Y 32
#define ROW_ARTIST_H 12
#define ROW_BAR_Y 46
#define ROW_BAR_H 10
#define ROW_TIME_Y 56
#define ROW_TIME_H 10
#define ROW_FORMAT_Y 66
#define ROW_FORMAT_H 10

#define RIGHT_X 196
#define RIGHT_W 84

#define ROW_TRANSPORT_Y 14
#define ROW_TRANSPORT_H 22
#define ROW_METER_Y 38
#define ROW_METER_H 36

/* Meter: 12 bars of 4 px with 2 px gutters is 70 px, centred in the column. */
#define METER_BAR_W 4
#define METER_GAP 2
#define METER_X (RIGHT_X + (RIGHT_W - (SPECTRUM_BARS * (METER_BAR_W + METER_GAP) - METER_GAP)) / 2)
#define METER_MAX_H 28

/* Home screen - four tiles ------------------------------------------------ */

/* Four tiles: 4 * 62 + 3 * 8 gutters + 6 px margins = 284. */
#define HOME_TILES 4
#define HOME_TILE_W 62
#define HOME_TILE_H 44
#define HOME_TILE_Y 20
#define HOME_TILE_GAP 8
#define HOME_TILE_X0 6

/* Menus - list and settings ---------------------------------------------- */

/* No menu header: the status bar already names the screen. */
#define MENU_Y (CONTENT_Y + 2)
#define MENU_ROW_H 12
#define MENU_ROWS ((GFX_H - MENU_Y) / MENU_ROW_H)

/*
 * A lane on the right belongs to the scroll rail. It is reserved whether or not
 * the list is long enough to need one, so rows do not shift when it appears.
 */
#define MENU_RAIL_W 2
#define MENU_RAIL_X (GFX_W - 4)
#define MENU_PILL_X 2
#define MENU_PILL_W (GFX_W - 9)
#define MENU_TEXT_X 6
#define MENU_TEXT_RIGHT (GFX_W - 10)

/* Overlays - volume, notices, messages ------------------------------------ */

/** Volume and notice card, centred in the content area. */
#define HUD_W 168
#define HUD_H 34
#define HUD_X ((GFX_W - HUD_W) / 2)
#define HUD_Y (CONTENT_Y + (CONTENT_H - HUD_H) / 2)

/** Message screen card - no card, no tracks, and whatever else goes wrong. */
#define MSG_W 208
#define MSG_H 46
#define MSG_X ((GFX_W - MSG_W) / 2)
#define MSG_Y (CONTENT_Y + (CONTENT_H - MSG_H) / 2)

#endif /* __THEME_H__ */
