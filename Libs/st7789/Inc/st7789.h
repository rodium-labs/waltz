/**
 * @file    st7789.h
 * @brief   Driver for the 2.25" 76x284 narrow-bar TFT (ST7789P3, 4-wire SPI).
 *          Driven in landscape by default - see ST7789_ROTATION.
 *
 * The controller carries a 240x320 frame memory but this panel only exposes a
 * 76x284 window in the middle of it, so every CASET/RASET pair has to be
 * shifted by ST7789_X_OFFSET / ST7789_Y_OFFSET. Being centred, the offsets are
 * the same whichever way the panel is mirrored.
 *
 * Pixel data goes out over DMA2_Stream3 as plain bytes, so buffers must hold
 * pixels already in wire order - GFX_WIRE_SWAP in gfx.h decides that.
 */

#ifndef __ST7789_H__
#define __ST7789_H__

#include <stdbool.h>
#include <stdint.h>

/**
 * Panel orientation.
 *
 *   0 - portrait  76x284
 *   1 - landscape 284x76        <- what the UI is laid out for
 *   2 - portrait  76x284, upside down
 *   3 - landscape 284x76, upside down
 *
 * If the picture is the right way round but mirrored end to end, switch between
 * 1 and 3 (or 0 and 2).
 */
#define ST7789_ROTATION 1

#if (ST7789_ROTATION & 1)
#define ST7789_W 284
#define ST7789_H 76
/* Landscape swaps the addressing axes, so the offsets swap with them. */
#define ST7789_X_OFFSET 18
#define ST7789_Y_OFFSET 82
#else
#define ST7789_W 76
#define ST7789_H 284
/** Column of frame memory that maps to panel x = 0. ((240 - 76) / 2) */
#define ST7789_X_OFFSET 82
/** Row of frame memory that maps to panel y = 0. ((320 - 284) / 2) */
#define ST7789_Y_OFFSET 18
#endif

/**
 * This panel reports colours in BGR order, so the controller is told to swap
 * them on the way into frame memory.
 */
#define ST7789_BGR 1

/**
 * Many IPS ST7789 panels need display inversion on; this one does not. Flip to
 * 1 if the image ever looks like a photo negative.
 */
#define ST7789_INVERT 0

/** Set to 1 when the module drives its backlight from BLK pulled low. */
#define ST7789_BLK_ACTIVE_LOW 0

/** Reset the panel, run the init sequence and turn the display on. */
void st7789_init(void);

/**
 * @brief Push a block of RGB565 pixels to the panel.
 * @param x,y  Top-left corner in panel coordinates.
 * @param w,h  Block size; must lie inside the panel.
 * @param px   w*h pixels, row-major.
 */
void st7789_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint16_t *px);

/** Flood a rectangle with a single colour without needing a pixel buffer. */
void st7789_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint16_t color);

/**
 * @brief Set backlight brightness, 0..100 % of light output.
 *
 * Compensates for ST7789_BLK_ACTIVE_LOW, so 0 is always dark and 100 always
 * fully lit. 50 is the one setting that lights the panel either way round,
 * which is what the boot-time panel check uses.
 */
void st7789_backlight(uint8_t percent);

#endif /* __ST7789_H__ */
