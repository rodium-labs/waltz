/**
 * @file    player.h
 * @brief   Playback state for the UI to render.
 *
 * There is no decoder and no SD card yet, so this is a mock: the transport
 * runs on a timer, the level meter is driven by a PRNG and the playlist is a
 * const table in flash. When real playback lands, only the guts of
 * Player_Tick() have to change - the UI reads nothing but this struct and calls
 * nothing but the transport functions below.
 */

#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <stdbool.h>
#include <stdint.h>

/** Bars in the level meter. */
#define SPECTRUM_BARS 12

/**
 * Nothing decodes audio yet, so the transport clock is simulated. Running it
 * faster than real time means track changes actually show up while testing.
 * Set to 1 for wall-clock timing.
 */
#define DEMO_TIME_SCALE 4

/**
 * Set to 1 to let the demo script drive the player: volume drifting on its own,
 * a scripted pause a third of the way into each track, shuffle and repeat
 * toggling at track boundaries.
 *
 * Off once there are buttons, because a self-drifting volume fights whatever
 * the user just pressed. The transport itself still advances - that is real
 * playback behaviour, not part of the script.
 */
#define PLAYER_DEMO 0

typedef struct {
  const char *title;
  const char *artist;
  uint16_t duration_s;
  uint16_t bitrate_kbps;
  /** Cover art gradient - each track gets its own palette. */
  uint16_t art_top;
  uint16_t art_bottom;
  uint16_t art_label;
} track_t;

typedef struct {
  uint8_t index;    /**< Current playlist slot.                          */
  uint8_t count;    /**< Playlist length.                                */
  uint16_t elapsed_s;
  bool playing;
  bool shuffle;
  bool repeat;
  uint8_t volume;   /**< 0..100 %                                        */
  uint8_t battery;  /**< 0..100 %                                        */
  uint8_t level[SPECTRUM_BARS];      /**< 0..100 per bar.                */
  uint8_t peak[SPECTRUM_BARS];       /**< Peak-hold marker, 0..100.       */
} player_t;

extern player_t player;

/**
 * @brief Is the player showing what a host is playing rather than the mock?
 *
 * True only when the setting is on *and* the link is talking. A cable pulled
 * mid-song falls back rather than freezing, which is the difference between a
 * stale screen and one that looks crashed.
 */
bool Player_ShellActive(uint32_t now);

/** Changes whenever the now-playing track does, whatever the source. */
uint16_t Player_TrackGeneration(void);

/** Starts paused on the first track: the home screen greets you, not audio. */
void Player_Init(void);

/** Advance transport, level meter and the demo script. @p now is HAL_GetTick(). */
void Player_Tick(uint32_t now);

const track_t *Player_Track(void);
const track_t *Player_NextTrack(void);
/** Playlist entry @p index, wrapped into range. */
const track_t *Player_TrackAt(uint8_t index);

/* Transport - what the buttons drive -------------------------------------- */

void Player_TogglePlay(void);
void Player_Next(void);
/**
 * Previous track, or restart the current one when more than
 * PLAYER_RESTART_WINDOW_S has elapsed - which is what every other player does,
 * and its absence is felt immediately.
 */
void Player_Prev(void);
/** Nudge the play position, clamped inside the track. */
void Player_Seek(int16_t delta_s);
/** Step the volume, clamped to 0..100. */
void Player_VolumeStep(int8_t delta);
/** Jump to a playlist entry and start it. */
void Player_Select(uint8_t index);
void Player_ToggleShuffle(void);
void Player_ToggleRepeat(void);
/**
 * @brief Step through the four play modes: off, shuffle, repeat, both.
 *
 * One gesture instead of two toggles, which is what makes it reachable from a
 * button chord rather than the settings page.
 */
void Player_CyclePlayMode(void);

/** How far into a track PREV stops meaning "previous" and starts meaning
 *  "start over". */
#define PLAYER_RESTART_WINDOW_S 3U

#endif /* __PLAYER_H__ */
