#include "power.h"

#include "main.h"

/**
 * Typical VREFINT for STM32F401, in millivolts. Unlike the L4 and G4 families
 * this part ships no factory calibration value, so the datasheet typical is all
 * there is - hence the +-2.5 % on the result.
 */
#define VREFINT_MV 1210U

#define ADC_FULL_SCALE 4095U

/** How often to convert. The rail does not move fast. */
#define POWER_PERIOD_MS 2000U

/** Percentage window: below this the rail is sagging, above it is healthy. */
#define RAIL_MIN_MV 2900U
#define RAIL_MAX_MV 3300U

static uint16_t supply_mv;
static uint32_t next_at;

void Power_Init(void) {
  supply_mv = 0;
  next_at = 0;
}

void Power_Tick(uint32_t now) {
  uint32_t raw;

  if (supply_mv != 0U && (now - next_at) < POWER_PERIOD_MS) {
    return;
  }
  next_at = now;

  if (HAL_ADC_Start(&hadc1) != HAL_OK) {
    return;
  }
  /* A single 12-bit conversion at 21 MHz with the long sampling VREFINT needs
   * is about 25 us, so polling it inline costs less than scheduling around it. */
  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK) {
    raw = HAL_ADC_GetValue(&hadc1);
    if (raw != 0U) {
      supply_mv = (uint16_t)(((uint32_t)ADC_FULL_SCALE * VREFINT_MV) / raw);
    }
  }
  (void)HAL_ADC_Stop(&hadc1);
}

uint16_t Power_SupplyMv(void) { return supply_mv; }

uint8_t Power_Percent(void) {
  uint32_t mv = supply_mv;

  if (mv == 0U) {
    return 100U; /* nothing measured yet - do not cry wolf */
  }
  if (mv <= RAIL_MIN_MV) {
    return 0U;
  }
  if (mv >= RAIL_MAX_MV) {
    return 100U;
  }
  return (uint8_t)(((mv - RAIL_MIN_MV) * 100U) / (RAIL_MAX_MV - RAIL_MIN_MV));
}

bool Power_HasBatterySense(void) { return false; }
