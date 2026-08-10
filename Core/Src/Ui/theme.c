#include "theme.h"

/*
 * Five schemes. Each one has to hold up on its own: three text weights that
 * stay legible against bg and card, and three accents that stay apart from each
 * other. The light scheme is here on purpose - it is the one that catches
 * anywhere the UI assumed a dark background.
 */
static const ui_theme_t themes[] = {
    {
        .name = "NIGHT",
        .bg = GFX_RGB(0x0A, 0x0A, 0x10),
        .card = GFX_RGB(0x17, 0x17, 0x22),
        .card_hi = GFX_RGB(0x27, 0x27, 0x36),
        .text = GFX_RGB(0xF2, 0xF2, 0xF7),
        .text_dim = GFX_RGB(0x96, 0x96, 0xAA),
        .text_mute = GFX_RGB(0x56, 0x56, 0x6C),
        .accent = GFX_RGB(0xFF, 0x3B, 0x6B),
        .accent2 = GFX_RGB(0x7C, 0x5C, 0xFF),
        .accent3 = GFX_RGB(0x22, 0xD3, 0xEE),
        .amber = GFX_RGB(0xFF, 0xB0, 0x3A),
        .green = GFX_RGB(0x35, 0xD0, 0x7F),
        .red = GFX_RGB(0xE0, 0x3B, 0x3B),
    },
    {
        .name = "AMBER",
        .bg = GFX_RGB(0x10, 0x0A, 0x06),
        .card = GFX_RGB(0x23, 0x18, 0x10),
        .card_hi = GFX_RGB(0x3A, 0x28, 0x1A),
        .text = GFX_RGB(0xFF, 0xF4, 0xE6),
        .text_dim = GFX_RGB(0xC0, 0xA1, 0x83),
        .text_mute = GFX_RGB(0x7A, 0x64, 0x48),
        .accent = GFX_RGB(0xFF, 0x8A, 0x3C),
        .accent2 = GFX_RGB(0xC2, 0x41, 0x0C),
        .accent3 = GFX_RGB(0xFF, 0xC9, 0x6B),
        .amber = GFX_RGB(0xFF, 0xB0, 0x3A),
        .green = GFX_RGB(0x8F, 0xBF, 0x4A),
        .red = GFX_RGB(0xE0, 0x50, 0x3B),
    },
    {
        .name = "MINT",
        .bg = GFX_RGB(0x05, 0x10, 0x0E),
        .card = GFX_RGB(0x0F, 0x23, 0x20),
        .card_hi = GFX_RGB(0x1B, 0x3A, 0x34),
        .text = GFX_RGB(0xE8, 0xFF, 0xF8),
        .text_dim = GFX_RGB(0x8F, 0xC7, 0xB8),
        .text_mute = GFX_RGB(0x4E, 0x7A, 0x6F),
        .accent = GFX_RGB(0x2E, 0xE0, 0xA8),
        .accent2 = GFX_RGB(0x0E, 0x74, 0x90),
        .accent3 = GFX_RGB(0x7D, 0xF9, 0xD0),
        .amber = GFX_RGB(0xE8, 0xD2, 0x6B),
        .green = GFX_RGB(0x35, 0xD0, 0x7F),
        .red = GFX_RGB(0xE0, 0x60, 0x3B),
    },
    {
        .name = "PAPER",
        .bg = GFX_RGB(0xF4, 0xF1, 0xEA),
        .card = GFX_RGB(0xE2, 0xDC, 0xCE),
        .card_hi = GFX_RGB(0xC6, 0xBE, 0xAC),
        .text = GFX_RGB(0x1B, 0x1A, 0x17),
        .text_dim = GFX_RGB(0x5E, 0x5A, 0x50),
        .text_mute = GFX_RGB(0x93, 0x8C, 0x7C),
        .accent = GFX_RGB(0xC2, 0x18, 0x5B),
        .accent2 = GFX_RGB(0x5B, 0x3F, 0xA8),
        .accent3 = GFX_RGB(0x00, 0x70, 0x7F),
        .amber = GFX_RGB(0xA6, 0x6A, 0x00),
        .green = GFX_RGB(0x2E, 0x7D, 0x32),
        .red = GFX_RGB(0xC6, 0x28, 0x28),
    },
    {
        .name = "TERMINAL",
        .bg = GFX_RGB(0x02, 0x06, 0x04),
        .card = GFX_RGB(0x07, 0x14, 0x0A),
        .card_hi = GFX_RGB(0x0D, 0x24, 0x12),
        .text = GFX_RGB(0x9C, 0xFF, 0xB0),
        .text_dim = GFX_RGB(0x4F, 0xBF, 0x6B),
        .text_mute = GFX_RGB(0x2A, 0x6B, 0x39),
        .accent = GFX_RGB(0x5B, 0xFF, 0x8A),
        .accent2 = GFX_RGB(0x1F, 0x8F, 0x44),
        .accent3 = GFX_RGB(0xA8, 0xFF, 0xC0),
        .amber = GFX_RGB(0xC8, 0xFF, 0x5B),
        .green = GFX_RGB(0x5B, 0xFF, 0x8A),
        .red = GFX_RGB(0xFF, 0x6B, 0x6B),
    },
};

#define THEME_COUNT ((uint8_t)(sizeof(themes) / sizeof(themes[0])))

static uint8_t current;

const ui_theme_t *ui_theme = &themes[0];

uint8_t Theme_Count(void) { return THEME_COUNT; }

uint8_t Theme_Index(void) { return current; }

const char *Theme_Name(uint8_t index) {
  return themes[index % THEME_COUNT].name;
}

void Theme_Set(uint8_t index) {
  current = (uint8_t)(index % THEME_COUNT);
  ui_theme = &themes[current];
}

void Theme_Next(void) { Theme_Set((uint8_t)(current + 1U)); }
