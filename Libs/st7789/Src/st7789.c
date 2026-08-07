#include "st7789.h"

#include "main.h"

/* Command set (ST7789P3 datasheet, chapter 9) ------------------------------ */
#define CMD_SWRESET 0x01
#define CMD_SLPOUT 0x11
#define CMD_INVOFF 0x20
#define CMD_INVON 0x21
#define CMD_DISPON 0x29
#define CMD_CASET 0x2A
#define CMD_RASET 0x2B
#define CMD_RAMWR 0x2C
#define CMD_MADCTL 0x36
#define CMD_COLMOD 0x3A
#define CMD_PORCTRL 0xB2
#define CMD_GCTRL 0xB7
#define CMD_VCOMS 0xBB
#define CMD_LCMCTRL 0xC0
#define CMD_VDVVRHEN 0xC2
#define CMD_VRHS 0xC3
#define CMD_VDVS 0xC4
#define CMD_FRCTRL2 0xC6
#define CMD_PWCTRL1 0xD0
#define CMD_PVGAMCTRL 0xE0
#define CMD_NVGAMCTRL 0xE1
#define CMD_NORON 0x13

/* MADCTL bits */
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_BGR 0x08

/* MV exchanges the row and column axes, which is what turns the bar panel on
 * its side; MX/MY pick which way round it ends up. The offsets are centred in
 * frame memory, so they stay the same whichever mirror bits are set. */
#if ST7789_ROTATION == 0
#define MADCTL_ROTATION 0x00
#elif ST7789_ROTATION == 1
#define MADCTL_ROTATION (MADCTL_MX | MADCTL_MV)
#elif ST7789_ROTATION == 2
#define MADCTL_ROTATION (MADCTL_MX | MADCTL_MY)
#else
#define MADCTL_ROTATION (MADCTL_MY | MADCTL_MV)
#endif

#if ST7789_BGR
#define MADCTL_VALUE (MADCTL_ROTATION | MADCTL_BGR)
#else
#define MADCTL_VALUE (MADCTL_ROTATION)
#endif

/* Staging run for colour fills. Lives in SRAM for the DMA to walk. */
#define FILL_RUN 128U
static uint16_t fill_run[FILL_RUN];

/* Pin helpers -------------------------------------------------------------- */

static inline void cs_low(void) {
  LCD_CS_GPIO_Port->BSRR = (uint32_t)LCD_CS_Pin << 16U;
}

static inline void cs_high(void) { LCD_CS_GPIO_Port->BSRR = LCD_CS_Pin; }

static inline void dc_command(void) {
  LCD_DC_GPIO_Port->BSRR = (uint32_t)LCD_DC_Pin << 16U;
}

static inline void dc_data(void) { LCD_DC_GPIO_Port->BSRR = LCD_DC_Pin; }

/* Command path - 8 bit, plain HAL polling. Only used at init and to set up
 * each window, so throughput does not matter here. */

static void wr_cmd(uint8_t cmd) {
  dc_command();
  HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
}

static void wr_args(const uint8_t *args, uint16_t len) {
  dc_data();
  HAL_SPI_Transmit(&hspi1, (uint8_t *)args, len, 500);
}

/** Command + parameters with CS already held low by the caller. */
static void wr_cmd_args_nocs(uint8_t cmd, const uint8_t *args, uint16_t len) {
  wr_cmd(cmd);
  if (len) {
    wr_args(args, len);
  }
}

/**
 * Command + parameters as a self-contained transaction.
 *
 * CS is pulsed per command rather than held low across the whole init: some
 * ST7789 clones reset their internal bit counter on the CS rising edge, and
 * without it a long unbroken stream can lose command framing - which shows up
 * as a panel that never leaves its power-on white.
 */
static void wr_cmd_args(uint8_t cmd, const uint8_t *args, uint16_t len) {
  cs_low();
  wr_cmd_args_nocs(cmd, args, len);
  cs_high();
}

/* Pixel path -------------------------------------------------------------- */

/**
 * Pixels leave as plain bytes, never as 16-bit frames.
 *
 * An earlier version widened SPI to DFF=16 for the pixel burst and switched
 * back for commands. That is the one thing this driver did that no other
 * ST7789 driver does, and once every controller-side colour configuration had
 * been ruled out on hardware - all eight of them, see Ui_ColorSweep() - a data
 * path that unusual was the only suspect left. Bytes now go out untouched, so
 * the caller's buffer must already hold pixels in wire order; GFX_WIRE_SWAP in
 * gfx.h owns that.
 */
static void spi_dma_begin(void) {
  while (SPI1->SR & SPI_SR_BSY) {
  }
  SPI1->CR2 |= SPI_CR2_TXDMAEN;
}

static void spi_dma_end(void) {
  while (!(SPI1->SR & SPI_SR_TXE)) {
  }
  while (SPI1->SR & SPI_SR_BSY) {
  }
  SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
}

/**
 * @brief Blocking DMA push of @p count bytes to SPI1.
 * @param src   Source address in SRAM.
 * @param count Number of bytes.
 */
static void dma_bytes(const void *src, uint32_t count) {
  while (count) {
    uint32_t chunk = (count > 65535U) ? 65535U : count;

    DMA2->LIFCR = DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3 | DMA_LIFCR_CTEIF3 |
                  DMA_LIFCR_CDMEIF3 | DMA_LIFCR_CFEIF3;

    DMA2_Stream3->PAR = (uint32_t)&SPI1->DR;
    DMA2_Stream3->M0AR = (uint32_t)src;
    DMA2_Stream3->NDTR = chunk;
    /* channel 3 = SPI1_TX, memory -> peripheral, byte wide both sides */
    DMA2_Stream3->CR = DMA_SxCR_CHSEL_0 | DMA_SxCR_CHSEL_1 | DMA_SxCR_DIR_0 |
                       DMA_SxCR_MINC | DMA_SxCR_PL_1;
    DMA2_Stream3->CR |= DMA_SxCR_EN;

    while (!(DMA2->LISR & DMA_LISR_TCIF3)) {
    }
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN) {
    }

    src = (const uint8_t *)src + chunk;
    count -= chunk;
  }
}

/** Repeat @p color for @p pixels pixels. Byte-wide DMA cannot alternate two
 *  bytes from one address, so a short run is staged and looped. */
static void dma_fill(uint32_t pixels, uint16_t color) {
  uint16_t i;

  for (i = 0; i < FILL_RUN; ++i) {
    fill_run[i] = color;
  }
  while (pixels) {
    uint32_t n = (pixels > FILL_RUN) ? FILL_RUN : pixels;
    dma_bytes(fill_run, n * 2U);
    pixels -= n;
  }
}

/**
 * Point the controller at a rectangle of frame memory and leave it expecting
 * pixel data. Coordinates are raw GRAM addresses - no panel offset applied.
 */
static void set_window_raw(uint16_t x0, uint16_t y0, uint16_t x1,
                           uint16_t y1) {
  uint8_t win[4];

  win[0] = (uint8_t)(x0 >> 8);
  win[1] = (uint8_t)x0;
  win[2] = (uint8_t)(x1 >> 8);
  win[3] = (uint8_t)x1;
  wr_cmd_args_nocs(CMD_CASET, win, 4);

  win[0] = (uint8_t)(y0 >> 8);
  win[1] = (uint8_t)y0;
  win[2] = (uint8_t)(y1 >> 8);
  win[3] = (uint8_t)y1;
  wr_cmd_args_nocs(CMD_RASET, win, 4);

  wr_cmd(CMD_RAMWR);
  dc_data();
}

/** Same, in panel coordinates - shifted onto the visible window. */
static void set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  set_window_raw((uint16_t)(x + ST7789_X_OFFSET),
                 (uint16_t)(y + ST7789_Y_OFFSET),
                 (uint16_t)(x + ST7789_X_OFFSET + w - 1U),
                 (uint16_t)(y + ST7789_Y_OFFSET + h - 1U));
}

/* Public API -------------------------------------------------------------- */

void st7789_init(void) {
  static const uint8_t porctrl[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  static const uint8_t pos_gamma[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                      0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
  static const uint8_t neg_gamma[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                      0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
  static const uint8_t pwctrl1[] = {0xA4, 0xA1};
  uint8_t arg;

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
  st7789_backlight(0);

  /* Hardware reset: the datasheet wants 10 us minimum low, then 120 ms before
   * the first command. Be generous, resets are cheap. */
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(150);

  wr_cmd_args(CMD_SWRESET, NULL, 0);
  HAL_Delay(150);
  wr_cmd_args(CMD_SLPOUT, NULL, 0);
  HAL_Delay(120);

  arg = 0x55; /* 16 bit / pixel, RGB565 */
  wr_cmd_args(CMD_COLMOD, &arg, 1);

  arg = MADCTL_VALUE;
  wr_cmd_args(CMD_MADCTL, &arg, 1);

  wr_cmd_args(CMD_PORCTRL, porctrl, sizeof(porctrl));

  arg = 0x35;
  wr_cmd_args(CMD_GCTRL, &arg, 1);
  arg = 0x19;
  wr_cmd_args(CMD_VCOMS, &arg, 1);
  arg = 0x2C;
  wr_cmd_args(CMD_LCMCTRL, &arg, 1);
  arg = 0x01;
  wr_cmd_args(CMD_VDVVRHEN, &arg, 1);
  arg = 0x12;
  wr_cmd_args(CMD_VRHS, &arg, 1);
  arg = 0x20;
  wr_cmd_args(CMD_VDVS, &arg, 1);
  arg = 0x0F; /* 60 Hz frame rate */
  wr_cmd_args(CMD_FRCTRL2, &arg, 1);

  wr_cmd_args(CMD_PWCTRL1, pwctrl1, sizeof(pwctrl1));
  wr_cmd_args(CMD_PVGAMCTRL, pos_gamma, sizeof(pos_gamma));
  wr_cmd_args(CMD_NVGAMCTRL, neg_gamma, sizeof(neg_gamma));

  wr_cmd_args(ST7789_INVERT ? CMD_INVON : CMD_INVOFF, NULL, 0);
  wr_cmd_args(CMD_NORON, NULL, 0);
  HAL_Delay(10);
  wr_cmd_args(CMD_DISPON, NULL, 0);
  HAL_Delay(120);

  /* Clear the whole panel before the backlight comes up, so the user never
   * sees the random contents frame memory holds after power-on. */
  st7789_fill(0, 0, ST7789_W, ST7789_H, 0x0000);
}

void st7789_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 const uint16_t *px) {
  if (!w || !h) {
    return;
  }

  cs_low();
  set_window(x, y, w, h);
  spi_dma_begin();
  dma_bytes(px, (uint32_t)w * h * 2U);
  spi_dma_end();
  cs_high();
}

void st7789_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint16_t color) {
  if (!w || !h) {
    return;
  }

  cs_low();
  set_window(x, y, w, h);
  spi_dma_begin();
  dma_fill((uint32_t)w * h, color);
  spi_dma_end();
  cs_high();
}

void st7789_set_color_mode(bool bgr, bool invert) {
  uint8_t madctl = (uint8_t)(MADCTL_ROTATION | (bgr ? MADCTL_BGR : 0U));

  wr_cmd_args(CMD_MADCTL, &madctl, 1);
  wr_cmd_args(invert ? CMD_INVON : CMD_INVOFF, NULL, 0);
}

void st7789_raw_fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                     uint16_t color) {
  uint32_t count = (uint32_t)(x1 - x0 + 1U) * (uint32_t)(y1 - y0 + 1U);

  cs_low();
  set_window_raw(x0, y0, x1, y1);
  spi_dma_begin();
  dma_fill(count, color);
  spi_dma_end();
  cs_high();
}

void st7789_gram_probe(void) {
  /* Stored byte-swapped so the wire gets RGB565 high byte first, matching
   * GFX_WIRE_SWAP in gfx.h. */
  const uint16_t red = 0x00F8U;   /* wire F8 00 */
  const uint16_t green = 0xE007U; /* wire 07 E0 */
  const uint16_t blue = 0x1F00U;  /* wire 00 1F */
  const uint16_t black = 0x0000U;

  /* Half duty stays visible whichever way round BLK is wired. */
  st7789_backlight(50);

  for (;;) {
    /* Stage 1 - the whole 240x320. White here means no commands are landing. */
    st7789_raw_fill(0, 0, ST7789_GRAM_W - 1U, ST7789_GRAM_H - 1U, red);
    HAL_Delay(1800);
    st7789_raw_fill(0, 0, ST7789_GRAM_W - 1U, ST7789_GRAM_H - 1U, black);
    HAL_Delay(500);

    /* Stage 2 - which third of the columns the 76 px window sits in. */
    st7789_raw_fill(0, 0, 79, ST7789_GRAM_H - 1U, red);
    st7789_raw_fill(80, 0, 159, ST7789_GRAM_H - 1U, green);
    st7789_raw_fill(160, 0, ST7789_GRAM_W - 1U, ST7789_GRAM_H - 1U, blue);
    HAL_Delay(3500);

    /* Stage 3 - which third of the rows the 284 px window sits in. */
    st7789_raw_fill(0, 0, ST7789_GRAM_W - 1U, 105, red);
    st7789_raw_fill(0, 106, ST7789_GRAM_W - 1U, 212, green);
    st7789_raw_fill(0, 213, ST7789_GRAM_W - 1U, ST7789_GRAM_H - 1U, blue);
    HAL_Delay(3500);
  }
}

void st7789_backlight(uint8_t percent) {
  if (percent > 100U) {
    percent = 100U;
  }
#if ST7789_BLK_ACTIVE_LOW
  percent = (uint8_t)(100U - percent);
#endif
  /* TIM2 ARR is 999, so percent maps straight onto tenths of a percent. */
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, (uint32_t)percent * 10U);
}
