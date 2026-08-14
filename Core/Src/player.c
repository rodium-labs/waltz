#include "player.h"

#include "settings.h"
#include "theme.h"

/* Titles are ASCII only - the fonts stop at 0x7E, so anything outside that would
 * render as '?' boxes. Placeholder data until ID3 tags come off an SD card; this
 * table is the only place the demo playlist lives. */
static const track_t playlist[] = {
    {
        .title = "Riders On The Storm",
        .artist = "The Doors",
        .duration_s = 245,
        .bitrate_kbps = 320,
        .art_top = GFX_RGB(0xFF, 0x6B, 0x35),
        .art_bottom = GFX_RGB(0x7A, 0x14, 0x53),
        .art_label = GFX_RGB(0xFF, 0xC1, 0x4D),
    },
    {
        .title = "Clocks",
        .artist = "Coldplay",
        .duration_s = 198,
        .bitrate_kbps = 256,
        .art_top = GFX_RGB(0x2E, 0xE0, 0xC0),
        .art_bottom = GFX_RGB(0x10, 0x2A, 0x6B),
        .art_label = GFX_RGB(0x9B, 0xFF, 0xE8),
    },
    {
        .title = "Everything In Its Right Place",
        .artist = "Radiohead",
        .duration_s = 312,
        .bitrate_kbps = 320,
        .art_top = GFX_RGB(0xC0, 0x7B, 0xFF),
        .art_bottom = GFX_RGB(0x1B, 0x14, 0x4A),
        .art_label = GFX_RGB(0xE8, 0xD5, 0xFF),
    },
    {
        .title = "Midnight City",
        .artist = "M83",
        .duration_s = 241,
        .bitrate_kbps = 192,
        .art_top = GFX_RGB(0xFF, 0x3B, 0x6B),
        .art_bottom = GFX_RGB(0x24, 0x0B, 0x36),
        .art_label = GFX_RGB(0xFF, 0xB0, 0x3A),
    },
    {
        .title = "Bohemian Rhapsody",
        .artist = "Queen",
        .duration_s = 355,
        .bitrate_kbps = 320,
        .art_top = GFX_RGB(0xF5, 0xD0, 0x76),
        .art_bottom = GFX_RGB(0x4A, 0x22, 0x0E),
        .art_label = GFX_RGB(0xFF, 0xF3, 0xD1),
    },
};

#define PLAYLIST_LEN ((uint8_t)(sizeof(playlist) / sizeof(playlist[0])))

/** Relative bar weights - real spectra fall off towards the treble end. */
static const uint8_t bar_shape[SPECTRUM_BARS] = {100, 96, 90, 82, 74, 66,
                                                 58,  50, 43, 36, 30, 24};

/** One accelerated "second" of playback. */
#define TRANSPORT_MS (1000U / DEMO_TIME_SCALE)
#define LEVEL_MS 50U
#define BEAT_MS 480U
/** How long the scripted "paused" moment lasts, in real milliseconds. */
#define DEMO_PAUSE_MS 2600U

player_t player;

static uint32_t transport_at;
static uint32_t level_at;
static uint32_t beat_at;
static uint32_t rng = 0x1F35DC91U;
/** Real milliseconds of playback, for the lifetime counter. */
static uint32_t play_ms;
static uint32_t play_last;
/** Accumulated playback seconds, used to move the wall clock along. */
static uint16_t second_acc;
/** Set once the current track has already shown off the paused state. */
static bool demo_pause_done;
#if PLAYER_DEMO
/** Tick at which the scripted pause ends. */
static uint32_t resume_at;
#endif

static uint8_t rnd(void) {
  rng = rng * 1664525U + 1013904223U;
  return (uint8_t)(rng >> 24);
}

void Player_Init(void) {
  uint8_t i;

  player.index = 0;
  player.count = PLAYLIST_LEN;
  player.elapsed_s = 0;
  /* Nothing plays until the user picks something from the home screen. */
  player.playing = false;
  player.shuffle = (settings.shuffle != 0U);
  player.repeat = (settings.repeat != 0U);
  player.volume = settings.volume;
  player.battery = 100;

  for (i = 0; i < SPECTRUM_BARS; ++i) {
    player.level[i] = 0;
    player.peak[i] = 0;
  }

  transport_at = 0;
  level_at = 0;
  beat_at = 0;
  second_acc = 0;
  demo_pause_done = false;
}

const track_t *Player_Track(void) { return &playlist[player.index]; }

const track_t *Player_NextTrack(void) {
  return &playlist[(player.index + 1U) % PLAYLIST_LEN];
}

const track_t *Player_TrackAt(uint8_t index) {
  return &playlist[index % PLAYLIST_LEN];
}

/* Transport --------------------------------------------------------------- */

/** Everything that starts a different track goes through here. */
static void begin_track(uint8_t index) {
  player.index = (uint8_t)(index % PLAYLIST_LEN);
  player.elapsed_s = 0;
  player.playing = true;
  demo_pause_done = false;
  stats.tracks++;
}


void Player_TogglePlay(void) { player.playing = !player.playing; }

void Player_Next(void) { begin_track((uint8_t)(player.index + 1U)); }

void Player_Prev(void) {
  if (player.elapsed_s > PLAYER_RESTART_WINDOW_S) {
    player.elapsed_s = 0;
    return;
  }
  begin_track((uint8_t)(player.index + PLAYLIST_LEN - 1U));
}

void Player_Seek(int16_t delta_s) {
  int32_t pos = (int32_t)player.elapsed_s + delta_s;
  uint16_t last = (uint16_t)(Player_Track()->duration_s - 1U);

  if (pos < 0) {
    pos = 0;
  } else if (pos > (int32_t)last) {
    pos = (int32_t)last;
  }
  player.elapsed_s = (uint16_t)pos;
}

void Player_VolumeStep(int8_t delta) {
  int16_t v = (int16_t)((int16_t)player.volume + delta);

  if (v < 0) {
    v = 0;
  } else if (v > 100) {
    v = 100;
  }
  player.volume = (uint8_t)v;
  settings.volume = player.volume;
}

void Player_Select(uint8_t index) { begin_track(index); }

void Player_ToggleShuffle(void) {
  player.shuffle = !player.shuffle;
  settings.shuffle = player.shuffle ? 1U : 0U;
}

void Player_ToggleRepeat(void) {
  player.repeat = !player.repeat;
  settings.repeat = player.repeat ? 1U : 0U;
}

void Player_CyclePlayMode(void) {
  if (!player.shuffle && !player.repeat) {
    player.shuffle = true;
  } else if (player.shuffle && !player.repeat) {
    player.shuffle = false;
    player.repeat = true;
  } else if (!player.shuffle && player.repeat) {
    player.shuffle = true;
  } else {
    player.shuffle = false;
    player.repeat = false;
  }
  settings.shuffle = player.shuffle ? 1U : 0U;
  settings.repeat = player.repeat ? 1U : 0U;
}

static void advance_track(void) {
  begin_track((uint8_t)(player.index + 1U));

#if PLAYER_DEMO
  /* Toggle the two mode flags on track boundaries so both states get seen. */
  player.shuffle = ((player.index & 1U) != 0U);
  player.repeat = ((player.index % 3U) == 2U);
#endif
}

#if PLAYER_DEMO
/** Volume walks up and down slowly so the bar is not a static prop. */
static void drift_volume(void) {
  static int8_t dir = 1;

  if ((player.elapsed_s % 4U) != 0U) {
    return;
  }
  player.volume = (uint8_t)(player.volume + dir);
  if (player.volume >= 84U) {
    dir = -1;
  } else if (player.volume <= 46U) {
    dir = 1;
  }
}
#endif /* PLAYER_DEMO */

static void tick_transport(uint32_t now) {
  const track_t *t = Player_Track();

  if (!player.playing) {
    return;
  }

  player.elapsed_s++;
#if PLAYER_DEMO
  drift_volume();
#endif

  second_acc++;

#if PLAYER_DEMO
  /* Demonstrate the paused layout once per track, a third of the way in. */
  if (!demo_pause_done && player.elapsed_s == (uint16_t)(t->duration_s / 3U)) {
    player.playing = false;
    demo_pause_done = true;
    resume_at = now + DEMO_PAUSE_MS;
  }
#else
  (void)now;
#endif

  if (player.elapsed_s >= t->duration_s) {
    if (player.repeat) {
      player.elapsed_s = 0;
      demo_pause_done = false;
    } else {
      advance_track();
    }
  }
}

/*
 * The level meter, and it is decorative: there is no audio to measure yet, so
 * the targets come from an LCG and a fixed beat, with bar_shape[] sloping the
 * bars downward so the result reads like a spectrum. Pausing drops it to zero,
 * which is most of why it looks connected to anything.
 *
 * When audio lands this is the function to replace. See the level meter entry
 * under "Not done yet" in the README for the two ways in - an FFT over the
 * decoded PCM, or reading the spectrum Helix already holds before it synthesises
 * that PCM. The attack and release below are worth keeping either way; a meter
 * fed raw magnitudes without them looks like noise.
 */
static void tick_level(bool beat) {
  uint8_t i;

  for (i = 0; i < SPECTRUM_BARS; ++i) {
    uint16_t target;

    if (player.playing) {
      /* 55..100 % of the bar's nominal weight, plus a kick on the low bars */
      target = (uint16_t)(((uint16_t)bar_shape[i] * (140U + (rnd() % 116U))) / 255U);
      if (beat && i < 4U) {
        target = (uint16_t)(target + 22U);
      }
      if (target > 100U) {
        target = 100U;
      }
    } else {
      target = 0;
    }

    if (target > player.level[i]) {
      /* attack: jump most of the way there */
      player.level[i] =
          (uint8_t)(player.level[i] + ((target - player.level[i]) * 3U) / 4U);
    } else {
      /* release: fall at a fixed rate so the meter looks damped */
      uint8_t fall = player.playing ? 5U : 9U;
      player.level[i] =
          (uint8_t)(player.level[i] > fall ? player.level[i] - fall : 0U);
    }

    /* Peak-hold marker: snaps up, sinks slowly - but not so slowly that a
     * paused meter leaves markers floating over empty bars. */
    if (player.level[i] >= player.peak[i]) {
      player.peak[i] = player.level[i];
    } else {
      uint8_t sink = player.playing ? 2U : 7U;
      player.peak[i] =
          (uint8_t)(player.peak[i] > sink ? player.peak[i] - sink : 0U);
      if (player.peak[i] < player.level[i]) {
        player.peak[i] = player.level[i];
      }
    }
  }
}

void Player_Tick(uint32_t now) {
  bool beat = false;

  /* Count wall-clock playback, not transport ticks - the transport runs fast
   * while there is nothing to decode, and the lifetime total should not. */
  if (player.playing && play_last != 0U) {
    play_ms += now - play_last;
    while (play_ms >= 1000U) {
      play_ms -= 1000U;
      stats.listen_s++;
    }
  }
  play_last = now;

  if (now - beat_at >= BEAT_MS) {
    beat_at = now;
    beat = true;
  }

  if (now - transport_at >= TRANSPORT_MS) {
    transport_at = now;
    tick_transport(now);
  }

  if (now - level_at >= LEVEL_MS) {
    level_at = now;
    tick_level(beat);
  }

#if PLAYER_DEMO
  if (!player.playing && (int32_t)(now - resume_at) >= 0) {
    player.playing = true;
  }
#endif
}
