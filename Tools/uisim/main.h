/* Host-side stand-in for Core/Inc/main.h so the gfx + UI layers can be built
 * and rendered on the Mac. Shadows the real header via -I ordering. */
#ifndef __MAIN_H
#define __MAIN_H

#include <stdint.h>

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);

#endif
