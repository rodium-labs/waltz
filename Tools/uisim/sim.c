/* Renders the real gfx/UI code into PPM files so the layout can be checked
 * without hardware. Only st7789_* and the two HAL time functions are faked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "input.h"
#include "player.h"
#include "player_ui.h"
#include "theme.h"
#include "st7789.h"

static uint16_t fb[ST7789_W * ST7789_H];
static uint32_t sim_tick;

/* Scripted button presses, so the preview covers both screens. input.c is not
 * compiled here - it reads GPIOA - so these stubs stand in for it. */
static input_event_t scripted[64];
static int script_head, script_tail;

void Input_Init(void) { script_head = script_tail = 0; }
void Input_Tick(uint32_t now) { (void)now; }

input_event_t Input_Get(void) {
  if (script_tail == script_head) {
    return INPUT_NONE;
  }
  return scripted[script_tail++];
}

static void press(input_event_t e) { scripted[script_head++] = e; }

uint32_t HAL_GetTick(void) { return sim_tick; }
void HAL_Delay(uint32_t ms) { sim_tick += ms; }

/* settings.c talks to the flash controller, so it is not built here. */
#include "settings.h"
settings_t settings = {0U, 4U, 2U, 2U, 68U, 0U, 0U, 0U};
stats_t stats = {7280U, 128U, 12U, 0U};
void Settings_Load(void) {}
bool Settings_Save(void) { return true; }
void Settings_Autosave(uint32_t now) { (void)now; }

/* power.c reaches for the ADC, so it is faked with a plausible rail. */
#include "power.h"
void Power_Init(void) {}
void Power_Tick(uint32_t now) { (void)now; }
uint16_t Power_SupplyMv(void) { return 3287U; }
uint8_t Power_Percent(void) { return 96U; }
bool Power_HasBatterySense(void) { return false; }

void st7789_init(void) {}
void st7789_backlight(uint8_t percent) { (void)percent; }
void st7789_set_color_mode(bool bgr, bool invert) { (void)bgr; (void)invert; }
void st7789_gram_probe(void) {}

void st7789_raw_fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color) {
  (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}

void st7789_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint16_t *px) {
  for (uint16_t r = 0; r < h; ++r) {
    for (uint16_t c = 0; c < w; ++c) {
      if (x + c < ST7789_W && y + r < ST7789_H) {
        fb[(y + r) * ST7789_W + (x + c)] = px[r * w + c];
      } else {
        fprintf(stderr, "BLIT OUT OF BOUNDS: %u,%u\n", x + c, y + r);
        exit(1);
      }
    }
  }
}

void st7789_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint16_t color) {
  for (uint16_t r = 0; r < h; ++r) {
    for (uint16_t c = 0; c < w; ++c) {
      if (x + c >= ST7789_W || y + r >= ST7789_H) {
        fprintf(stderr, "FILL OUT OF BOUNDS: %u,%u\n", x + c, y + r);
        exit(1);
      }
      fb[(y + r) * ST7789_W + (x + c)] = color;
    }
  }
}

static void dump(const char *path) {
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", ST7789_W, ST7789_H);
  for (int i = 0; i < ST7789_W * ST7789_H; ++i) {
    /* pixels are stored in wire order, so undo that before unpacking */
    uint16_t p = GFX_WIRE(fb[i]);
    uint8_t rgb[3];
    /* expand 5/6/5 back to 8 bits the way the panel does */
    rgb[0] = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
    rgb[1] = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63);
    rgb[2] = (uint8_t)((p & 0x1F) * 255 / 31);
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

/** Run the main loop for @p ms of virtual time in 5 ms steps. */
static void run_for(uint32_t ms) {
  uint32_t end = sim_tick + ms;
  while (sim_tick < end) {
    Player_Tick(sim_tick);
    Ui_Tick(sim_tick);
    sim_tick += 5;
  }
}

int main(int argc, char **argv) {
  const char *outdir = (argc > 1) ? argv[1] : ".";
  char path[512];
  int shot = 1;

#define SHOT(name)                                                             \
  do {                                                                         \
    snprintf(path, sizeof path, "%s/%02d-%s.ppm", outdir, shot++, name);        \
    dump(path);                                                                \
  } while (0)

  /* The splash now follows the stored theme, so render a few of them. */
  for (int t = 0; t < 10; t += 3) {
    settings.theme = (uint8_t)t;
    Ui_Splash();
    snprintf(path, sizeof path, "%s/S%02d-splash.ppm", outdir, t);
    dump(path);
  }
  settings.theme = 0U;
  Ui_Splash();
  SHOT("splash");

  Settings_Load();
  Input_Init();
  Player_Init();
  Ui_Init();
  run_for(300);
  SHOT("home");

  /* home: walk the tiles */
  press(INPUT_NEXT);
  run_for(100);
  SHOT("home-list");
  press(INPUT_NEXT);
  run_for(100);
  SHOT("home-settings");

  /* into the list, pick a track */
  press(INPUT_PREV);
  press(INPUT_PLAY);
  run_for(200);
  SHOT("list");
  press(INPUT_NEXT);
  press(INPUT_NEXT);
  run_for(100);
  SHOT("list-scrolled");
  press(INPUT_PLAY);
  run_for(3000);
  SHOT("player");

  /* let it run so the marquee and meter move */
  run_for(9000);
  SHOT("player-running");

  /*
   * Every theme, on the player screen - the stale-marquee-colour bug lived
   * there. The home tile is on TRACKS at this point, so MENU, PLAY, PLAY walks
   * home -> list -> player deterministically without guessing at selections.
   */
  for (int t = 0; t < 10; ++t) {
    Theme_Set((uint8_t)t);
    press(INPUT_MENU);
    run_for(80);
    press(INPUT_PLAY);
    run_for(80);
    press(INPUT_PLAY);
    run_for(600);
    snprintf(path, sizeof path, "%s/T%02d-theme.ppm", outdir, t);
    dump(path);
  }
  Theme_Set(0);
  press(INPUT_MENU);
  run_for(200);

  /* home -> stats */
  press(INPUT_MENU);
  run_for(100);
  press(INPUT_NEXT); /* TRACKS -> STATS */
  run_for(100);
  SHOT("home-stats");
  press(INPUT_PLAY);
  run_for(200);
  SHOT("stats");

  /* stats -> home -> settings */
  press(INPUT_MENU);
  run_for(100);
  press(INPUT_NEXT); /* STATS -> SETTINGS */
  run_for(100);
  press(INPUT_PLAY);
  run_for(200);
  SHOT("settings");

  /* down to the FADE row and cycle it */
  press(INPUT_NEXT);
  press(INPUT_NEXT);
  press(INPUT_NEXT);
  run_for(100);
  press(INPUT_PLAY);
  run_for(200);
  SHOT("settings-fade");

  /* cycle the play mode with the volume chord, from the settings page */
  press(INPUT_MODE);
  run_for(100);
  SHOT("mode-shuffle");
  press(INPUT_MODE);
  run_for(100);
  SHOT("mode-repeat");

  /* back out of settings, which is the save point */
  press(INPUT_MENU);
  run_for(200);
  SHOT("home-themed");

  printf("final: track %u/%u elapsed %us playing=%d vol=%u batt=%u\n",
         player.index + 1, player.count, player.elapsed_s, player.playing,
         player.volume, player.battery);
  return 0;
}
