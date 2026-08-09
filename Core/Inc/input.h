/**
 * @file    input.h
 * @brief   Five buttons on GPIOA, turned into debounced semantic events.
 *
 * All five sit on one port so a scan is a single IDR read, and they are polled
 * from the main loop rather than driven by EXTI: a press lasts 50-200 ms, the UI
 * loop runs far faster than that even with a full-frame blit in the way, and
 * polling makes debouncing four shift-register samples instead of an interrupt
 * plus timer per button.
 *
 * The events are semantic rather than raw edges, because the two screens want
 * different things from the same buttons:
 *
 *   VOL-/VOL+  fire on press and then auto-repeat while held
 *   PREV/NEXT  fire once on press, then repeat as a *_HOLD event so the caller
 *              can seek instead of skipping
 *   PLAY       fires INPUT_PLAY on release if it was a short press, or
 *              INPUT_MENU once at 600 ms - never both
 */

#ifndef __INPUT_H__
#define __INPUT_H__

#include <stdint.h>

typedef enum {
  INPUT_NONE = 0,
  INPUT_VOL_DOWN,
  INPUT_VOL_UP,
  INPUT_PREV,
  INPUT_NEXT,
  /** PREV/NEXT held down - seek on the player screen, fast scroll in the list. */
  INPUT_PREV_HOLD,
  INPUT_NEXT_HOLD,
  INPUT_PLAY,
  /** PLAY held - goes back. */
  INPUT_MENU,
  /**
   * Both volume buttons at once.
   *
   * Every short and long press across the five buttons is already spoken for,
   * so cycling the play mode needs a chord. The volume pair is the safe one to
   * overload: the first press of the pair still lands a volume step, and a
   * stray 2 % is harmless where a stray track skip would not be.
   */
  INPUT_MODE,
} input_event_t;

void Input_Init(void);

/** Sample the buttons. Cheap to call every loop; rate-limits itself. */
void Input_Tick(uint32_t now);

/** Pop the oldest event, or INPUT_NONE when the queue is empty. */
input_event_t Input_Get(void);

#endif /* __INPUT_H__ */
