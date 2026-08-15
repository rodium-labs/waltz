/**
 * @file    usbd_cdc_if.h
 * @brief   The CDC class's hooks into this application.
 *
 * Receive hands every byte to Shell_Feed(); nothing is sent back yet. The board
 * is a listener - the host does the talking, and a serial port that never
 * replies is one less thing to get wrong.
 */

#ifndef __USBD_CDC_IF_H__
#define __USBD_CDC_IF_H__

#include "usbd_cdc.h"

extern USBD_CDC_ItfTypeDef Waltz_CDC_fops;

#endif /* __USBD_CDC_IF_H__ */
