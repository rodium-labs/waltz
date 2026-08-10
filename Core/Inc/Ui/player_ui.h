/**
 * @file    player_ui.h
 * @brief   Now-playing screen for the 76x284 panel.
 */

#ifndef __PLAYER_UI_H__
#define __PLAYER_UI_H__

#include <stdint.h>

/**
 * Set to 1 to hold the six-strip colour check at boot. Off by default - the
 * splash already carries R/G/B swatches and the alignment frame, so the normal
 * boot self-tests colour order without costing 15 seconds.
 */
#define UI_PANEL_CHECK 0

/**
 * @brief Four labelled colour bands, held for three seconds.
 *
 * Runs the backlight at 50 %, which lights the panel whichever way round BLK
 * is wired - so this screen appears even if ST7789_BLK_ACTIVE_LOW is set the
 * wrong way, and it is therefore the test that tells backlight problems apart
 * from SPI problems.
 *
 * - Bands visible and matching their labels: SPI, init and colour order are
 *   all fine.
 * - Bands visible but red and blue swapped: flip ST7789_BGR.
 * - Plain white or noise: the panel is not receiving valid data.
 */
void Ui_PanelCheck(void);

/**
 * @brief Walk every 16-bit colour format the controller can be in. Never
 *        returns.
 *
 * Shows eight numbered candidates, four seconds each, every one drawing three
 * strips labelled RED / GREEN / BLUE. Whichever number renders all three
 * labels in their own colour identifies the format: the header line spells out
 * that candidate's flags as S<byteswap> B<bgr> I<invert>, which map onto
 * ST7789_BGR, ST7789_INVERT and the pixel byte order.
 */
void Ui_ColorSweep(void);

/**
 * @brief Boot screen, which doubles as the panel alignment check.
 *
 * Draws a 1 px border around all 284 rows plus R/G/B swatches, then fades the
 * backlight in. If any edge of the border is missing, or the swatches are not
 * red-green-blue left to right, the offsets or ST7789_BGR in st7789.h need
 * adjusting. Blocks for about 1.5 s.
 */
void Ui_Splash(void);

/** Paint the full player screen and latch the current player state. */
void Ui_Init(void);

/** Repaint whatever changed since the last call. @p now is HAL_GetTick(). */
void Ui_Tick(uint32_t now);

#endif /* __PLAYER_UI_H__ */
