/**
 * @file    usbd_cdc_if.c
 * @brief   Where received bytes leave USB behind and become shell lines.
 *
 * Everything the host sends goes straight to Shell_Feed() from the USB
 * interrupt. That is deliberate: the parser only touches its own line buffer
 * and the shell state, the main loop only reads that state, and a byte can
 * never be dropped because the loop was busy painting a frame.
 *
 * The line control requests are answered but ignored. There is no UART behind
 * this - baud rate and stop bits mean nothing to a device that is already the
 * endpoint - and hosts get upset if the requests fail.
 */

#include "usbd_cdc_if.h"

#include "shell.h"

/* One full-speed packet each way. The receive buffer is handed straight back to
 * the stack after every packet, so it never needs to hold more. */
static uint8_t rx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t tx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];

/** What the host thinks the line looks like. Stored so it can be read back. */
static uint8_t line_coding[7] = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08};

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t cdc_init(void) {
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_buffer, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_buffer);
  return USBD_OK;
}

static int8_t cdc_deinit(void) { return USBD_OK; }

static int8_t cdc_control(uint8_t cmd, uint8_t *pbuf, uint16_t length) {
  (void)length;

  switch (cmd) {
  case CDC_SET_LINE_CODING:
    line_coding[0] = pbuf[0];
    line_coding[1] = pbuf[1];
    line_coding[2] = pbuf[2];
    line_coding[3] = pbuf[3];
    line_coding[4] = pbuf[4];
    line_coding[5] = pbuf[5];
    line_coding[6] = pbuf[6];
    break;
  case CDC_GET_LINE_CODING:
    pbuf[0] = line_coding[0];
    pbuf[1] = line_coding[1];
    pbuf[2] = line_coding[2];
    pbuf[3] = line_coding[3];
    pbuf[4] = line_coding[4];
    pbuf[5] = line_coding[5];
    pbuf[6] = line_coding[6];
    break;
  default:
    /* Everything else - line state, break, the rest - is meaningless here and
     * answering OK is what keeps a host from retrying forever. */
    break;
  }
  return USBD_OK;
}

static int8_t cdc_receive(uint8_t *buf, uint32_t *len) {
  uint32_t i;

  for (i = 0U; i < *len; ++i) {
    Shell_Feed(buf[i]);
  }

  /* Hand the buffer back before returning, or the endpoint stays NAKed and the
   * host stalls after one packet. */
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &rx_buffer[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return USBD_OK;
}

static int8_t cdc_transmit_complete(uint8_t *buf, uint32_t *len, uint8_t epnum) {
  (void)buf;
  (void)len;
  (void)epnum;
  return USBD_OK;
}

USBD_CDC_ItfTypeDef Waltz_CDC_fops = {cdc_init, cdc_deinit, cdc_control,
                                      cdc_receive, cdc_transmit_complete};
