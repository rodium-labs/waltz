/**
 * @file    power.h
 * @brief   Supply-rail measurement over the ADC's internal reference.
 *
 * There is no battery and no divider on this board, and - having checked - no
 * pin left to put one on: ADC1 only reaches PA0..PA7, PB0 and PB1 on this
 * package, and every one of those is already a button or a display line. So the
 * one thing that *can* be measured without adding hardware is VDDA itself, via
 * the internal 1.21 V reference:
 *
 *     VDDA = 4095 * VREFINT_mV / adc_reading
 *
 * That makes it a brown-out gauge rather than a fuel gauge, which is genuinely
 * useful when running off an ST-Link's weak 3.3 V rail. Accuracy is limited by
 * the reference spread, +-2.5 % on this part, so about +-80 mV.
 *
 * For a real battery: move VOL+ off PA6 to PB6, then PA6 (ADC1_IN6) is free for
 * a divider and Power_BatteryMv() has something to read.
 */

#ifndef __POWER_H__
#define __POWER_H__

#include <stdbool.h>
#include <stdint.h>

void Power_Init(void);

/** Take a reading if one is due. Cheap to call every loop. */
void Power_Tick(uint32_t now);

/** Supply rail in millivolts, 0 before the first reading lands. */
uint16_t Power_SupplyMv(void);

/**
 * @brief Rail health as a percentage, 2.90 V to 3.30 V mapped onto 0..100.
 *
 * Not a state of charge - nothing here knows about a cell. It reads full on a
 * healthy rail and falls as the rail sags, which is the useful signal.
 */
uint8_t Power_Percent(void);

/** True once a battery divider exists and Power_BatteryMv() means something. */
bool Power_HasBatterySense(void);

#endif /* __POWER_H__ */
