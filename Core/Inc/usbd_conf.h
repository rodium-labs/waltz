/**
 * @file    usbd_conf.h
 * @brief   Sizing and platform glue for ST's USB device library.
 *
 * One interface, one class, one string at a time - this is a serial port, not a
 * composite device. Allocation is static: the library asks for one class
 * instance and there is no heap on this build to give it from.
 */

#ifndef __USBD_CONF_H__
#define __USBD_CONF_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#define USBD_MAX_NUM_INTERFACES 1U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 512U
#define USBD_DEBUG_LEVEL 0U
#define USBD_LPM_ENABLED 0U
#define USBD_SELF_POWERED 0U

/* The board draws from the bus, and 100 mA is what the panel plus the core
 * actually take with the backlight at full. */
#define USBD_MAX_POWER 100U

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#define USBD_malloc (void *)USBD_static_malloc
#define USBD_free USBD_static_free
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_Delay HAL_Delay

#if (USBD_DEBUG_LEVEL > 0U)
#define USBD_UsrLog(...)                                                       \
  printf(__VA_ARGS__);                                                         \
  printf("\n");
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1U)
#define USBD_ErrLog(...)                                                       \
  printf("ERROR: ");                                                           \
  printf(__VA_ARGS__);                                                         \
  printf("\n");
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2U)
#define USBD_DbgLog(...)                                                       \
  printf("DEBUG : ");                                                          \
  printf(__VA_ARGS__);                                                         \
  printf("\n");
#else
#define USBD_DbgLog(...)
#endif

#endif /* __USBD_CONF_H__ */
