#include "shell.h"

#include <string.h>

#include "main.h"

shell_t shell;

/** Anything quieter than this and the host is assumed gone. */
#define SHELL_IDLE_MS 2000U

/*
 * One line at a time. Longer lines are truncated rather than dropped, because a
 * title that arrives clipped is still better than a title that never arrives,
 * and the UI truncates anyway.
 */
#define SHELL_LINE_MAX 96
static char line[SHELL_LINE_MAX];
static uint8_t len;

void Shell_Init(void) {
  memset(&shell, 0, sizeof shell);
  len = 0U;
}

/** Decimal, stopping at the first thing that is not a digit. */
static uint16_t parse_u16(const char *s) {
  uint32_t v = 0U;

  while (*s >= '0' && *s <= '9') {
    v = v * 10U + (uint32_t)(*s - '0');
    if (v > 65535U) {
      return 65535U;
    }
    ++s;
  }
  return (uint16_t)v;
}

static uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return (uint8_t)(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return (uint8_t)(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return (uint8_t)(c - 'A' + 10);
  }
  return 0xFFU;
}

/** Copy, and say whether it actually changed anything. */
static bool copy_text(char *dst, const char *src) {
  char before[SHELL_TEXT_MAX];
  uint8_t i = 0U;

  memcpy(before, dst, SHELL_TEXT_MAX);

  while (src[i] != '\0' && i < (SHELL_TEXT_MAX - 1U)) {
    /* The font covers 32..126; anything else would draw as '?' anyway, and a
     * host sending UTF-8 should not smear multi-byte sequences across it. */
    dst[i] = (src[i] >= 32 && src[i] <= 126) ? src[i] : '?';
    ++i;
  }
  dst[i] = '\0';
  return memcmp(before, dst, SHELL_TEXT_MAX) != 0;
}

static void take_line(void) {
  const char *arg = &line[1];
  uint8_t i;

  switch (line[0]) {
  case 'T':
    if (copy_text(shell.title, arg)) {
      shell.generation++;
    }
    break;
  case 'A':
    if (copy_text(shell.artist, arg)) {
      shell.generation++;
    }
    break;
  case 'D':
    shell.duration_s = parse_u16(arg);
    break;
  case 'E':
    shell.elapsed_s = parse_u16(arg);
    break;
  case 'P':
    shell.playing = (arg[0] != '0');
    break;
  case 'V': {
    uint16_t v = parse_u16(arg);

    shell.volume = (uint8_t)((v > 100U) ? 100U : v);
    break;
  }
  case 'B':
    for (i = 0U; i < SPECTRUM_BARS; ++i) {
      uint8_t hi = hex_nibble(arg[i * 2U]);
      uint8_t lo = hex_nibble(arg[i * 2U + 1U]);
      uint16_t v;

      if (hi == 0xFFU || lo == 0xFFU) {
        break; /* short or malformed line - keep what did parse */
      }
      v = (uint16_t)((hi << 4) | lo);
      shell.level[i] = (uint8_t)((v > 100U) ? 100U : v);
    }
    shell.has_bars = true;
    break;
  default:
    /* Unknown command. Ignored on purpose - see the protocol note in shell.h. */
    return;
  }

  shell.fed_at = HAL_GetTick();
  shell.seen = true;
}

void Shell_Feed(uint8_t byte) {
  if (byte == '\r') {
    return;
  }
  if (byte == '\n') {
    line[len] = '\0';
    if (len > 0U) {
      take_line();
    }
    len = 0U;
    return;
  }
  if (len < (SHELL_LINE_MAX - 1U)) {
    line[len++] = (char)byte;
  }
}

bool Shell_Live(uint32_t now) {
  return shell.seen && ((now - shell.fed_at) < SHELL_IDLE_MS);
}
