#include "settings.h"

#include "main.h"

/*
 * Sector 5 - the last 128 kB of the 256 kB part, at 0x08020000. The firmware is
 * well under 64 kB and lives in sectors 0-3, so there is no chance of it growing
 * into this one.
 */
#define CFG_SECTOR FLASH_SECTOR_5
#define CFG_BASE 0x08020000UL
#define CFG_SIZE 0x20000UL

/* Bumped whenever the record layout changes, so old records are ignored rather
 * than misread. */
#define CFG_MAGIC 0x4D503332UL /* "MP32" */

/** Commit the counters at most this often. */
#define AUTOSAVE_MS (5U * 60U * 1000U)

typedef struct {
  uint32_t magic;
  settings_t data;
  stats_t counters;
  uint16_t sum;
  uint16_t pad;      /* keeps the record a round 32 bytes */
  uint32_t reserved;
} cfg_record_t;

#define CFG_SLOTS (CFG_SIZE / sizeof(cfg_record_t))

/** Defaults, used until a stored record turns up. */
settings_t settings = {
    .theme = 0U,
    .brightness = 4U,
    .blank = 0U,
    .fade = 3U, /* SLOW - the gentlest ramp reads best */
    .volume = 68U,
    .shuffle = 0U,
    .repeat = 0U,
    .shell = 0U, /* off: a board on a desk with no cable should still play */
};

stats_t stats;

static uint32_t saved_listen_s;
static uint32_t autosave_at;

static uint16_t checksum(const void *data, uint16_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint16_t sum = 0x1234U;
  uint16_t i;

  for (i = 0; i < len; ++i) {
    sum = (uint16_t)((uint16_t)(sum << 3) ^ (uint16_t)(sum >> 13) ^ p[i]);
  }
  return sum;
}

static uint16_t record_sum(const cfg_record_t *r) {
  uint16_t sum = checksum(&r->data, (uint16_t)sizeof(settings_t));

  return (uint16_t)(sum ^ checksum(&r->counters, (uint16_t)sizeof(stats_t)));
}

static const cfg_record_t *slot(uint32_t index) {
  return (const cfg_record_t *)(CFG_BASE + index * sizeof(cfg_record_t));
}

static bool slot_valid(const cfg_record_t *r) {
  return (r->magic == CFG_MAGIC) && (r->sum == record_sum(r));
}

static bool slot_erased(const cfg_record_t *r) {
  return r->magic == 0xFFFFFFFFUL;
}

void Settings_Load(void) {
  uint32_t i;
  const cfg_record_t *newest = NULL;

  for (i = 0; i < CFG_SLOTS; ++i) {
    const cfg_record_t *r = slot(i);

    if (slot_erased(r)) {
      break; /* records are appended, so the first blank ends the run */
    }
    if (slot_valid(r)) {
      newest = r;
    }
  }

  if (newest) {
    settings = newest->data;
    stats = newest->counters;
  }
  saved_listen_s = stats.listen_s;

  /* A boot is a session whether or not anything else changes. */
  stats.sessions++;
}

static bool erase_sector(void) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t error = 0;

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = CFG_SECTOR;
  erase.NbSectors = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  return HAL_FLASHEx_Erase(&erase, &error) == HAL_OK;
}

bool Settings_Save(void) {
  cfg_record_t record;
  uint32_t i;
  uint32_t target = 0;
  bool found = false;
  const uint32_t *words = (const uint32_t *)&record;
  bool ok = true;

  record.magic = CFG_MAGIC;
  record.data = settings;
  record.counters = stats;
  record.sum = record_sum(&record);
  record.pad = 0xFFFFU;
  record.reserved = 0xFFFFFFFFUL;

  for (i = 0; i < CFG_SLOTS; ++i) {
    if (slot_erased(slot(i))) {
      target = i;
      found = true;
      break;
    }
  }

  HAL_FLASH_Unlock();

  if (!found) {
    /* Sector full - wipe it and start over. Costs about a second, once every
     * CFG_SLOTS saves. */
    if (!erase_sector()) {
      HAL_FLASH_Lock();
      return false;
    }
    target = 0;
  }

  for (i = 0; i < sizeof(cfg_record_t) / sizeof(uint32_t); ++i) {
    uint32_t address =
        CFG_BASE + target * sizeof(cfg_record_t) + i * sizeof(uint32_t);

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, words[i]) !=
        HAL_OK) {
      ok = false;
      break;
    }
  }

  HAL_FLASH_Lock();

  ok = ok && slot_valid(slot(target));
  if (ok) {
    saved_listen_s = stats.listen_s;
  }
  return ok;
}

void Settings_Autosave(uint32_t now) {
  if (now - autosave_at < AUTOSAVE_MS) {
    return;
  }
  autosave_at = now;
  if (stats.listen_s != saved_listen_s) {
    (void)Settings_Save();
  }
}
