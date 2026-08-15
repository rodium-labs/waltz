/**
 * @file    usbd_desc.c
 * @brief   What the board says it is when a host asks.
 *
 * The VID/PID pair is ST's own CDC pair, which is what every ST CDC device
 * ships with and what macOS and Linux already know how to bind a serial driver
 * to. It is not ours; if this ever leaves a desk it needs a real one.
 *
 * The serial number is derived from the die's unique ID so two boards on the
 * same machine do not collide.
 */

#include "usbd_core.h"
#include "usbd_def.h"

#define USBD_VID 0x0483
#define USBD_PID 0x5740
#define USBD_LANGID_STRING 0x0409 /* en-US */
#define USBD_MANUFACTURER_STRING "Rodium Labs"
#define USBD_PRODUCT_STRING "Waltz"
#define USBD_CONFIGURATION_STRING "Shell"
#define USBD_INTERFACE_STRING "Waltz shell"

static uint8_t *dev_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *langid_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *manufacturer_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *product_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *serial_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *config_desc(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *interface_desc(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef Waltz_Desc = {
    dev_desc,          langid_desc,  manufacturer_desc, product_desc,
    serial_desc,       config_desc,  interface_desc,
};

/* Descriptors the peripheral DMAs from, so they are aligned. */
__ALIGN_BEGIN static uint8_t device_descriptor[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,                 /* bLength */
    USB_DESC_TYPE_DEVICE, /* bDescriptorType */
    0x00, 0x02,           /* bcdUSB 2.00 */
    0x02,                 /* bDeviceClass: CDC */
    0x02,                 /* bDeviceSubClass */
    0x00,                 /* bDeviceProtocol */
    USB_MAX_EP0_SIZE,     /* bMaxPacketSize */
    LOBYTE(USBD_VID), HIBYTE(USBD_VID),
    LOBYTE(USBD_PID), HIBYTE(USBD_PID),
    0x00, 0x02,           /* bcdDevice 2.00 */
    USBD_IDX_MFC_STR,     /* iManufacturer */
    USBD_IDX_PRODUCT_STR, /* iProduct */
    USBD_IDX_SERIAL_STR,  /* iSerialNumber */
    USBD_MAX_NUM_CONFIGURATION};

__ALIGN_BEGIN static uint8_t langid_descriptor[USB_LEN_LANGID_STR_DESC]
    __ALIGN_END = {USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING,
                   LOBYTE(USBD_LANGID_STRING), HIBYTE(USBD_LANGID_STRING)};

__ALIGN_BEGIN static uint8_t string_scratch[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN static uint8_t serial_string[26] __ALIGN_END = {
    26, USB_DESC_TYPE_STRING};

static void hex_to_unicode(uint32_t value, uint8_t *out, uint8_t digits) {
  uint8_t i;

  for (i = 0U; i < digits; ++i) {
    uint8_t nib = (uint8_t)((value >> (28U - 4U * i)) & 0x0FU);

    out[2U * i] = (uint8_t)((nib < 10U) ? ('0' + nib) : ('A' + nib - 10U));
    out[2U * i + 1U] = 0U;
  }
}

/** Twelve bytes of die ID folded into eight hex digits. */
static void build_serial(void) {
  uint32_t a = *(uint32_t *)UID_BASE;
  uint32_t b = *(uint32_t *)(UID_BASE + 4U);
  uint32_t c = *(uint32_t *)(UID_BASE + 8U);

  hex_to_unicode(a + c, &serial_string[2], 8U);
  hex_to_unicode(b, &serial_string[18], 4U);
}

static uint8_t *dev_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  *length = sizeof device_descriptor;
  return device_descriptor;
}

static uint8_t *langid_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  *length = sizeof langid_descriptor;
  return langid_descriptor;
}

static uint8_t *product_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)USBD_PRODUCT_STRING, string_scratch, length);
  return string_scratch;
}

static uint8_t *manufacturer_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, string_scratch, length);
  return string_scratch;
}

static uint8_t *serial_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  *length = sizeof serial_string;
  build_serial();
  return serial_string;
}

static uint8_t *config_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING, string_scratch, length);
  return string_scratch;
}

static uint8_t *interface_desc(USBD_SpeedTypeDef speed, uint16_t *length) {
  (void)speed;
  USBD_GetString((uint8_t *)USBD_INTERFACE_STRING, string_scratch, length);
  return string_scratch;
}
