/**
 * @file    shell.h
 * @brief   What a host is playing, fed in over a link.
 *
 * There is no SD card and no decoder yet, so the only real music around is on
 * the machine at the other end of the cable. Shell mode borrows it: the host
 * says what it is playing and how loud each band is, and the player screen
 * shows that instead of the mocked playlist. It is the first time the UI sees
 * titles it did not choose, timings it does not control, and a level meter that
 * is not a random number generator.
 *
 * The transport is deliberately not part of this. Bytes arrive from wherever -
 * USB CDC today - and Shell_Feed() is the whole interface. Nothing above here
 * knows or cares.
 */

#ifndef __SHELL_H__
#define __SHELL_H__

#include <stdbool.h>
#include <stdint.h>

#include "player.h" /* SPECTRUM_BARS, and the field widths below match it */

/** Longest title or artist kept. Anything longer is truncated, as on the UI. */
#define SHELL_TEXT_MAX 48

/**
 * @brief The line protocol, one command per line, terminated by \n.
 *
 * ASCII on purpose: it can be driven by hand from a serial terminal, which is
 * how every field gets tested before any host script exists.
 *
 *   T<text>    title
 *   A<text>    artist
 *   D<secs>    duration
 *   E<secs>    elapsed
 *   P<0|1>     paused / playing
 *   V<0..100>  host volume
 *   B<hex>     SPECTRUM_BARS levels, two hex digits each, 00..64
 *
 * Anything else is ignored rather than rejected: a host that learns to send a
 * new field must not break a board that has not learned to read it.
 */
typedef struct {
  char title[SHELL_TEXT_MAX];
  char artist[SHELL_TEXT_MAX];
  uint16_t duration_s;
  uint16_t elapsed_s;
  uint8_t volume;
  bool playing;
  uint8_t level[SPECTRUM_BARS];

  /**
   * Bumped whenever the title or artist actually changes. The UI latches the
   * marquee text once per track rather than every frame, and in shell mode the
   * track changes without any index changing - so this is what says "different
   * song now".
   */
  uint16_t generation;

  /** Tick of the last complete line. Staleness is judged from this. */
  uint32_t fed_at;
  /** True once anything at all has arrived, so the UI can say "waiting". */
  bool seen;
  /**
   * True once the host has sent a single B line. Until then the meter keeps
   * running off the mock: a host with no way to see the audio should leave the
   * bars alive rather than frozen at whatever they last were, and pretending to
   * measure something is worse than either.
   */
  bool has_bars;
} shell_t;

extern shell_t shell;

void Shell_Init(void);

/** Push one received byte. Safe to call from an interrupt. */
void Shell_Feed(uint8_t byte);

/**
 * @brief Is the host talking to us right now?
 *
 * False when nothing has arrived for a couple of seconds, so an unplugged cable
 * shows as a stalled screen rather than a frozen one that looks like a crash.
 */
bool Shell_Live(uint32_t now);

#endif /* __SHELL_H__ */
