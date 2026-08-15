/**
 * @file    usb_device.h
 * @brief   Bring the USB device up.
 */

#ifndef __USB_DEVICE_H__
#define __USB_DEVICE_H__

#include <stdint.h>

/* Device index handed to USBD_Init. There is one peripheral, so it is zero -
 * the library only uses it to tell several devices apart. */
#define DEVICE_FS 0

/** Register the CDC class and start the peripheral. Safe to call once. */
void USB_Device_Init(void);

#endif /* __USB_DEVICE_H__ */
