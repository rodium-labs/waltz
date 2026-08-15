/**
 * @file    usbd_conf.c
 * @brief   Between ST's USB device library and the HAL's PCD driver.
 *
 * The library speaks USBD_LL_*; the peripheral speaks HAL_PCD_*. This file is
 * the translation in both directions and nothing else.
 *
 * Two things here are board rather than family specific. PA11 and PA12 are the
 * only USB pins wired on a Black Pill - no ID, no VBUS sense - so VBUS sensing
 * is off and the peripheral is put in device mode outright. And the FIFO split
 * is the one from ST's own FS CDC example: 128 words of receive, 64 for control
 * and 128 for the bulk endpoint, which is 320 of the 320 word FIFO.
 */

#include "usbd_cdc.h"
#include "usbd_core.h"
#include "usbd_def.h"

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* HAL -> library --------------------------------------------------------- */

static USBD_StatusTypeDef usb_status(HAL_StatusTypeDef s) {
  switch (s) {
  case HAL_OK:
    return USBD_OK;
  case HAL_BUSY:
    return USBD_BUSY;
  default:
    return USBD_FAIL;
  }
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcdHandle) {
  GPIO_InitTypeDef gpio = {0};

  if (pcdHandle->Instance != USB_OTG_FS) {
    return;
  }

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* DM and DP. Nothing else on the connector is wired on this board. */
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &gpio);

  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

  /* Below the panel's DMA in priority: a late frame is visible, a late USB
   * packet is not. */
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle) {
  if (pcdHandle->Instance != USB_OTG_FS) {
    return;
  }
  __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
  HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_SetupStage((USBD_HandleTypeDef *)hpcd->pData,
                     (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum) {
  USBD_LL_DataOutStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                       hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum) {
  USBD_LL_DataInStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                      hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_SetSpeed((USBD_HandleTypeDef *)hpcd->pData, USBD_SPEED_FULL);
  USBD_LL_Reset((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_Suspend((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_Resume((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum) {
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum) {
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_DevConnected((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) {
  USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}

/* library -> HAL --------------------------------------------------------- */

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev) {
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;

  hpcd_USB_OTG_FS.pData = pdev;
  pdev->pData = &hpcd_USB_OTG_FS;

  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK) {
    Error_Handler();
  }

  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x40);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x80);

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev) {
  return usb_status(HAL_PCD_DeInit(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev) {
  return usb_status(HAL_PCD_Start(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev) {
  return usb_status(HAL_PCD_Stop(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                  uint8_t ep_type, uint16_t ep_mps) {
  return usb_status(HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type));
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr) {
  return usb_status(HAL_PCD_EP_Close(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr) {
  return usb_status(HAL_PCD_EP_Flush(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr) {
  return usb_status(HAL_PCD_EP_SetStall(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,
                                        uint8_t ep_addr) {
  return usb_status(HAL_PCD_EP_ClrStall(pdev->pData, ep_addr));
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr) {
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;

  if ((ep_addr & 0x80U) == 0x80U) {
    return hpcd->IN_ep[ep_addr & 0x7FU].is_stall;
  }
  return hpcd->OUT_ep[ep_addr & 0x7FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,
                                         uint8_t dev_addr) {
  return usb_status(HAL_PCD_SetAddress(pdev->pData, dev_addr));
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                    uint8_t *pbuf, uint32_t size) {
  return usb_status(HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size));
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
                                          uint8_t ep_addr, uint8_t *pbuf,
                                          uint32_t size) {
  return usb_status(HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size));
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr) {
  return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

/* Static allocation ------------------------------------------------------- */

/* One class instance, and the library only ever asks for one. */
static uint32_t class_block[((sizeof(USBD_CDC_HandleTypeDef) / 4U) + 1U)];

void *USBD_static_malloc(uint32_t size) {
  (void)size;
  return class_block;
}

void USBD_static_free(void *p) { (void)p; }
