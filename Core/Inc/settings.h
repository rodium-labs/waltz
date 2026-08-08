/**
 * @file    settings.h
 * @brief   User settings, kept across power-off in the last flash sector.
 *
 * Flash bits only go 1 -> 0 without an erase, and erasing the 128 kB sector
 * takes about a second of stalled CPU. So instead of rewriting one slot, each
 * save appends a fresh 16-byte record and the loader takes the last valid one.
 * That makes a save a couple of word writes, and the erase only happens once
 * the sector fills - 8192 saves later.
 */

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t theme;      /**< Theme index.                                    */
  uint8_t brightness; /**< Index into the brightness steps.                */
  uint8_t blank;      /**< Index into the screen-off timeouts.             */
  uint8_t fade;       /**< Index into the backlight fade speeds.           */
  uint8_t volume;     /**< 0..100 %.                                       */
  uint8_t shuffle;
  uint8_t repeat;
  uint8_t reserved;   /**< Keeps the struct a round 8 bytes.               */
} settings_t;

/**
 * @brief Lifetime counters for the stats screen.
 *
 * @c listen_s counts real seconds of playback, not transport ticks, so it stays
 * honest regardless of DEMO_TIME_SCALE. @c cycles stays at zero until there is
 * a battery to count cycles of.
 */
typedef struct {
  uint32_t listen_s;
  uint32_t tracks;
  uint16_t sessions;
  uint16_t cycles;
} stats_t;

extern settings_t settings;
extern stats_t stats;

/** Fill @c settings from flash, or leave the defaults if nothing is stored. */
void Settings_Load(void);

/**
 * @brief Append the current settings to flash.
 * @return true when the record was written and verified.
 *
 * Called when leaving the settings screen rather than on every keypress, so
 * holding a button does not burn through slots.
 */
bool Settings_Save(void);

/**
 * @brief Save if the counters have moved and enough time has passed.
 *
 * Listening time changes every second and there is no power-down warning to
 * flush on, so the counters are committed periodically instead. Losing the last
 * few minutes to a yanked cable beats a flash write every second.
 */
void Settings_Autosave(uint32_t now);

#endif /* __SETTINGS_H__ */
