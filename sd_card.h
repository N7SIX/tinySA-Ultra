/**
 * sd_card.h - Shared SPI bus primitives for the SD card driver (sd_card.c).
 *
 * The SD card and the LCD share the SPI1 bus; the SD driver lives in
 * sd_card.c, while the bus primitives and the SPI mode arbiter live in
 * ili9341.c, which includes this header.
 */
#ifndef __SD_CARD_H__
#define __SD_CARD_H__

#ifdef __USE_SD_CARD__

// SD card SPI bus (shared with the LCD, see ili9341.c)
#define SD_SPI            SPI1

// Define SD SPI speed on work
#define SD_SPI_SPEED        SPI_BR_DIV2
// Define SD SPI speed on initialization (100-400kHz need)
#define SD_INIT_SPI_SPEED   SPI_BR_DIV256

#ifdef TINYSA4
// SPI1 bus arbitration pin control (shared between ili9341.c and sd_card.c).
// The LCD and the SD card share SPI1; only one CS may be asserted at a time.
#define LCD_CS_LOW        palClearPad(GPIO_LCD_CS_PORT, GPIO_LCD_CS)
#define LCD_CS_HIGH       palSetPad(GPIO_LCD_CS_PORT, GPIO_LCD_CS)
#define SD_CS_LOW         palClearLine(LINE_SD_CS)
#define SD_CS_HIGH        palSetLine(LINE_SD_CS)

// SPI bus mode arbiter (implemented in ili9341.c)
extern void set_SPI_mode(uint16_t mode);
#endif

// SPI bus primitives (implemented in ili9341.c)
extern void spi_TxByte(uint8_t data);
extern void spi_TxWord(uint16_t data);
extern void spi_TxBuffer(const uint8_t *buffer, uint16_t len);
extern uint8_t spi_RxByte(void);
extern void spi_RxBuffer(uint8_t *buffer, uint16_t len);
extern void spi_DropRx(void);

#ifdef __USE_DISPLAY_DMA__
extern void spi_DMATxBuffer(const uint8_t *buffer, uint16_t len, bool wait);
extern void spi_DMARxBuffer(uint8_t *buffer, uint16_t len, bool wait);
#endif

// SD_Inserted() / SD_PowerOff() are declared in nanovna.h;
// the FatFs disk_* hooks (DSTATUS / DRESULT) come in via nanovna.h
// (FatFs/ff.h + diskio.h), which sd_card.c includes before this header.

#endif //__USE_SD_CARD__

#endif //__SD_CARD_H__
