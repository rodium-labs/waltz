/**
 * @file    gfx_fonts.h
 * @brief   1bpp bitmap font descriptors.
 */

#ifndef __GFX_FONTS_H__
#define __GFX_FONTS_H__

#include <stddef.h>
#include <stdint.h>

/**
 * @brief A 1bpp font covering ASCII 32..126.
 *
 * @c data holds @c height consecutive uint16_t per glyph, one word per pixel
 * row, left-aligned to the MSB - so pixel @c j of a row is
 * @c (row << j) & 0x8000.
 */
typedef struct {
  uint8_t width;  /**< Cell width; also the number of meaningful bits/row.  */
  uint8_t height; /**< Cell height, i.e. words per glyph.                   */
  const uint16_t *data;
  /** Per-glyph advance for proportional faces; NULL when monospaced. */
  const uint8_t *char_width;
} gfx_font_t;

extern const gfx_font_t Font_Mono6x8;
extern const gfx_font_t Font_Mono7x10;
extern const gfx_font_t Font_Mono11x18;
/** Roboto Thin 15 - proportional, good for track titles. */
extern const gfx_font_t Font_Roboto16;

#endif /* __GFX_FONTS_H__ */
