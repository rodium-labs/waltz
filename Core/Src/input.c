#include "input.h"

#include <stdbool.h>

#include "main.h"

#define POLL_MS 5U
/** Four consecutive agreeing samples at 5 ms - 20 ms of debounce. */
#define DEBOUNCE_MASK 0x0FU
/** Held this long and PLAY means "menu" instead of "play/pause". */
#define LONG_MS 600U
/** Auto-repeat: wait, then step, then step faster. Without this, walking the
 *  volume from 20 % to 80 % would be thirty presses. */
#define REPEAT_DELAY_MS 400U
#define REPEAT_MS 100U
#define REPEAT_FAST_AFTER_MS 1200U
#define REPEAT_FAST_MS 50U

#define EVENT_QUEUE 8U

typedef struct {
  uint16_t pin;
  /** Fired on the debounced press. INPUT_NONE for click/long buttons. */
  input_event_t on_press;
  /** Fired repeatedly while held. */
  input_event_t on_repeat;
  /** Fired on release, only if the long event did not fire first. */
  input_event_t on_click;
  /** Fired once after LONG_MS. */
  input_event_t on_long;
} btn_def_t;

static const btn_def_t buttons[] = {
    {BTN_VOL_DOWN_Pin, INPUT_VOL_DOWN, INPUT_VOL_DOWN, INPUT_NONE, INPUT_NONE},
    {BTN_PREV_Pin, INPUT_PREV, INPUT_PREV_HOLD, INPUT_NONE, INPUT_NONE},
    {BTN_PLAY_Pin, INPUT_NONE, INPUT_NONE, INPUT_PLAY, INPUT_MENU},
    {BTN_NEXT_Pin, INPUT_NEXT, INPUT_NEXT_HOLD, INPUT_NONE, INPUT_NONE},
    {BTN_VOL_UP_Pin, INPUT_VOL_UP, INPUT_VOL_UP, INPUT_NONE, INPUT_NONE},
};

#define BTN_COUNT (sizeof(buttons) / sizeof(buttons[0]))

static struct {
  uint8_t history;
  bool down;
  bool long_fired;
  uint32_t down_at;
  uint32_t next_repeat;
} state[BTN_COUNT];

/** Set while both volume buttons are held, to mute their auto-repeat. */
static bool chord_active;

static input_event_t queue[EVENT_QUEUE];
static uint8_t q_head;
static uint8_t q_tail;
static uint32_t poll_at;

static void push(input_event_t e) {
  uint8_t next = (uint8_t)((q_head + 1U) % EVENT_QUEUE);

  if (e == INPUT_NONE || next == q_tail) {
    return; /* nothing to say, or the consumer has fallen behind */
  }
  queue[q_head] = e;
  q_head = next;
}

void Input_Init(void) {
  uint8_t i;

  for (i = 0; i < BTN_COUNT; ++i) {
    state[i].history = 0;
    state[i].down = false;
    state[i].long_fired = false;
  }
  q_head = 0;
  q_tail = 0;
  poll_at = 0;
  chord_active = false;
}

/** Index into `buttons` - the chord is the two volume keys. */
#define BTN_VOL_DOWN_IDX 0U
#define BTN_VOL_UP_IDX 4U

void Input_Tick(uint32_t now) {
  uint32_t idr;
  uint8_t i;

  if (now - poll_at < POLL_MS) {
    return;
  }
  poll_at = now;

  /* One read covers all five - that is why they share a port. */
  idr = BTN_GPIO_Port->IDR;

  for (i = 0; i < BTN_COUNT; ++i) {
    /* Pull-ups, buttons to ground, so a pressed button reads low. */
    bool raw = ((idr & buttons[i].pin) == 0U);
    uint8_t hist;

    state[i].history = (uint8_t)((state[i].history << 1) | (raw ? 1U : 0U));
    hist = (uint8_t)(state[i].history & DEBOUNCE_MASK);

    if (!state[i].down && hist == DEBOUNCE_MASK) {
      state[i].down = true;
      state[i].long_fired = false;
      state[i].down_at = now;
      state[i].next_repeat = now + REPEAT_DELAY_MS;
      push(buttons[i].on_press);
    } else if (state[i].down && hist == 0U) {
      state[i].down = false;
      if (!state[i].long_fired) {
        push(buttons[i].on_click);
      }
    }

    if (state[i].down) {
      uint32_t held = now - state[i].down_at;

      if (!state[i].long_fired && buttons[i].on_long != INPUT_NONE &&
          held >= LONG_MS) {
        state[i].long_fired = true;
        push(buttons[i].on_long);
      }
      bool muted = chord_active && (i == BTN_VOL_DOWN_IDX ||
                                    i == BTN_VOL_UP_IDX);

      if (!muted && buttons[i].on_repeat != INPUT_NONE &&
          (int32_t)(now - state[i].next_repeat) >= 0) {
        push(buttons[i].on_repeat);
        state[i].next_repeat =
            now + ((held >= REPEAT_FAST_AFTER_MS) ? REPEAT_FAST_MS : REPEAT_MS);
      }
    }
  }

  /* Chord: both volume keys down together. Fires once per press of the pair. */
  {
    bool both = state[BTN_VOL_DOWN_IDX].down && state[BTN_VOL_UP_IDX].down;

    if (both && !chord_active) {
      chord_active = true;
      push(INPUT_MODE);
    } else if (!state[BTN_VOL_DOWN_IDX].down && !state[BTN_VOL_UP_IDX].down) {
      chord_active = false;
    }
  }
}

input_event_t Input_Get(void) {
  input_event_t e;

  if (q_tail == q_head) {
    return INPUT_NONE;
  }
  e = queue[q_tail];
  q_tail = (uint8_t)((q_tail + 1U) % EVENT_QUEUE);
  return e;
}
