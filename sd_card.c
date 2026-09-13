//******************************************************************************
// SD Card support for tinySA (extracted from ili9341.c)
//
// The SD card shares the SPI1 bus with the LCD. All bus arbitration
// (SD_CS / LCD_CS / bus speed switching) happens here and in ili9341.c,
// which includes sd_card.h for the shared SPI primitives.
//******************************************************************************
#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "spi.h"
#include "sd_card.h"

#ifdef __USE_SD_CARD__
//*****************************************************
//* SD functions and definitions
//*****************************************************
// Definitions for MMC/SDC command
#define CMD0     (0x40+0)     // GO_IDLE_STATE
#define CMD1     (0x40+1)     // SEND_OP_COND
#define CMD8     (0x40+8)     // SEND_IF_COND
#define CMD9     (0x40+9)     // SEND_CSD
#define CMD10    (0x40+10)    // SEND_CID
#define CMD12    (0x40+12)    // STOP_TRANSMISSION
#define CMD13    (0x40+13)    // SEND_STATUS
#define CMD16    (0x40+16)    // SET_BLOCKLEN
#define CMD17    (0x40+17)    // READ_SINGLE_BLOCK
#define CMD18    (0x40+18)    // READ_MULTIPLE_BLOCK
#define CMD23    (0x40+23)    // SET_BLOCK_COUNT
#define CMD24    (0x40+24)    // WRITE_BLOCK
#define CMD25    (0x40+25)    // WRITE_MULTIPLE_BLOCK
#define CMD55    (0x40+55)    // APP_CMD
#define CMD58    (0x40+58)    // READ_OCR
#define CMD59    (0x40+59)    // CRC_ON_OFF
// Then send after CMD55 (APP_CMD) interpret as ACMD
#define ACMD41   (0x40+41)    // SEND_OP_COND (ACMD)

// MMC card type flags (MMC_GET_TYPE)
#define CT_MMC      0x01      // MMC v3
#define CT_SD1      0x02      // SDv1
#define CT_SD2      0x04      // SDv2
#define CT_SDC      0x06      // SD
#define CT_BLOCK    0x08      // Block addressing

// 7.3.2 Responses
// 7.3.2.1 Format R1 (1 byte)
// This response token is sent by the card after every command with the exception of SEND_STATUS commands.
#define SD_R1_IDLE                                 ((uint8_t)0x01) // The card is in idle state
#define SD_R1_ERASE_RESET                          ((uint8_t)0x02) // erase reset
#define SD_R1_ILLEGAL_CMD                          ((uint8_t)0x04) // Illegal command
#define SD_R1_CRC_ERROR                            ((uint8_t)0x08) // The CRC check of the last command failed
#define SD_R1_ERR_ERASE_CLR                        ((uint8_t)0x10) // error in the sequence of erase commands
#define SD_R1_ADDR_ERROR                           ((uint8_t)0x20) // Incorrect address specified
#define SD_R1_PARAM_ERROR                          ((uint8_t)0x40) // Parameter error
#define SD_R1_NOT_R1                               ((uint8_t)0x80) // Not R1 register
// 7.3.2.2 Format R1b (R1 + Busy)
// The busy signal token can be any number of bytes. A zero value indicates card is busy.
// A non-zero value indicates the card is ready for the next command.
// 7.3.2.3 Format R2  (2 byte)
// This response token is two bytes long and sent as a response to the SEND_STATUS command.
// 1 byte - some as R1
// 2 byte -

// 7.3.2.4 Format R3 (R1 + OCR, 5 bytes)
// This response token is sent by the card when a READ_OCR command is received.
// 1 byte - some as R1
// 2-5 byte - OCR
// On Send byte order in SendCommand send MSB first!!
// Received byte order MSB last!!
#define _OCR(dword) (((dword&0x000000FF)<<24)|((dword&0x0000FF00)<<8)|((dword&0x00FF0000)>>8)|((dword&0xFF000000)>>24))
#define SD_OCR_LOW_VOLTAGE                         ((uint32_t)0x00000080) // Reserved for Low Voltage Range
#define SD_OCR_27_VOLTAGE                          ((uint32_t)0x00008000) // VDD Voltage Window 2.7-2.8V
#define SD_OCR_28_VOLTAGE                          ((uint32_t)0x00010000) // VDD Voltage Window 2.8-2.9V
#define SD_OCR_29_VOLTAGE                          ((uint32_t)0x00020000) // VDD Voltage Window 2.9-3.0V
#define SD_OCR_30_VOLTAGE                          ((uint32_t)0x00040000) // VDD Voltage Window 3.0-3.1V
#define SD_OCR_31_VOLTAGE                          ((uint32_t)0x00080000) // VDD Voltage Window 3.1-3.2V
#define SD_OCR_32_VOLTAGE                          ((uint32_t)0x00100000) // VDD Voltage Window 3.2-3.3V
#define SD_OCR_33_VOLTAGE                          ((uint32_t)0x00200000) // VDD Voltage Window 3.3-3.4V
#define SD_OCR_34_VOLTAGE                          ((uint32_t)0x00400000) // VDD Voltage Window 3.4-3.8V
#define SD_OCR_35_VOLTAGE                          ((uint32_t)0x00800000) // VDD Voltage Window 3.5-3.6V
#define SD_OCR_18_VOLTAGE                          ((uint32_t)0x01000000) // VDD Voltage switch to 1.8V (UHS-I only)
#define SD_OCR_CAPACITY                            ((uint32_t)0x40000000) // Card Capacity Status (CCS)
#define SD_OCR_BUSY                                ((uint32_t)0x80000000) // Card power up status bit (busy)

// 5.3 CSD Register
//  16GB Kingston  40 0E 00 32 5B 59 00 00 73 A7 7F 80 0A 40 00 EB   //  29608 * 512 kB
//  32GB Samsung   40 0E 00 32 5B 59 00 00 EE 7F 7F 80 0A 40 40 55   //  61056 * 512 kB
// 128GB Samsung   40 0E 00 32 5B 59 00 03 B9 FF 7F 80 0A 40 40 AB   // 244224 * 512 kB
#define CSD_0_STRUCTURE                          0b11000000
#define CSD_1_TAAC                               0b11111111
#define CSD_2_NSAC                               0b11111111
#define CSD_3_TRAN_SPEED                         0b11111111
#define CSD_4_CCC                                0b11111111
#define CSD_5_CCC                                0b11110000
#define CSD_5_READ_BL_LEN                        0b00001111
#define CSD_6_READ_BL_PARTIAL                    0b10000000
#define CSD_6_WRITE_BLK_MISALIGN                 0b01000000
#define CSD_6_READ_BLK_MISALIGN                  0b00100000
#define CSD_6_DSR_IMP                            0b00010000
#define CSD_7_C_SIZE                             0b00111111
#define CSD_8_C_SIZE                             0b11111111
#define CSD_9_C_SIZE                             0b11111111
#define CSD_10_ERASE_BLK_EN                      0b01000000
#define CSD_10_SECTOR_SIZE                       0b00111111
#define CSD_11_SECTOR_SIZE                       0b10000000
#define CSD_11_WP_GRP_SIZE                       0b01111111
#define CSD_12_WP_GRP_ENABLE                     0b10000000
#define CSD_12_R2W_FACTOR                        0b00011100
#define CSD_12_WRITE_BL_LEN                      0b00000011
#define CSD_13_WRITE_BL_LEN                      0b11000000
#define CSD_13_WRITE_BL_PARTIAL                  0b00100000
#define CSD_14_FILE_FORMAT_GRP                   0b10000000
#define CSD_14_COPY                              0b01000000
#define CSD_14_PERM_WRITE_PROTECT                0b00100000
#define CSD_14_TMP_WRITE_PROTECT                 0b00010000
#define CSD_14_FILE_FORMAT                       0b00001100
#define CSD_15_CRC                               0b11111110


// 7.3.3.1 Data Response Token
#define SD_TOKEN_DATA_ACCEPTED                     ((uint8_t)0x05) // Data accepted
#define SD_TOKEN_WRITE_CRC_ERROR                   ((uint8_t)0x0b) // Data rejected due to a CRC error
#define SD_TOKEN_WRITE_ERROR                       ((uint8_t)0x0d) // Data rejected due to a write error
// 7.3.3.2 Start Block Tokens and Stop Tran Token
#define SD_TOKEN_START_BLOCK                       ((uint8_t)0xfe) // Start block (single tx, single/multiple rx)
#define SD_TOKEN_START_M_BLOCK                     ((uint8_t)0xfc) // Start multiple block tx
#define SD_TOKEN_STOP_M_BLOCK                      ((uint8_t)0xfd) // Stop multiple block tx
// 7.3.3.3 Data Error Token
#define SD_TOKEN_READ_ERROR                        ((uint8_t)0x01) // Data read error
#define SD_TOKEN_READ_CC_ERROR                     ((uint8_t)0x02) // Internal card controller error
#define SD_TOKEN_READ_ECC_ERROR                    ((uint8_t)0x04) // Card ECC failed
#define SD_TOKEN_READ_RANGE_ERROR                  ((uint8_t)0x08) // Read address out of range

//*****************************************************
//             SD card module settings
//*****************************************************
// Additional state flag definition
#define STA_POWER_ON       0x80   // Power ON flag

// Use for enable CRC check of Tx and Rx data on SPI
// If enable both CRC check, on initialization send SD command - CRC_ON_OFF vs ON
// And Card begin check received data and answer on CRC errors
//#define SD_USE_COMMAND_CRC
//#define SD_USE_DATA_CRC

// Use DMA on sector data Tx to SD card (only if enabled Tx DMA for LCD)
#ifdef __USE_DISPLAY_DMA__
#define __USE_SDCARD_DMA__
#endif

// Use DMA on sector data Rx from SD card (only if enabled Rx DMA for LCD)
#ifdef __USE_DISPLAY_DMA_RX__
#define __USE_SDCARD_DMA_RX__
#endif

// Define sector size
#define SD_SECTOR_SIZE      512
// Set number of try read or write sector data (1 only one try)
#define SD_READ_WRITE_REPEAT 1
// Local values for SD card state
static DSTATUS Stat = STA_NOINIT;  // Disk Status
static uint8_t CardType  = 0;      // Type 0:MMC, 1:SDC, 2:Block addressing

// Debug functions, 0 to disable
#define DEBUG    0
int shell_printf(const char *fmt, ...);
#define DEBUG_PRINT(...) do { if (DEBUG) shell_printf(__VA_ARGS__); } while (0)
#if DEBUG == 1
uint32_t w_cnt;
uint32_t w_time;
uint32_t r_cnt;
uint32_t r_time;
uint32_t total_time;
uint32_t crc_time;
void testLog(void){
  DEBUG_PRINT(" Read  speed = %d Byte/s (count %d, time %d)\r\n", r_cnt*512*10000/r_time, r_cnt, r_time);
  DEBUG_PRINT(" Write speed = %d Byte/s (count %d, time %d)\r\n", w_cnt*512*10000/w_time, w_cnt, w_time);
  DEBUG_PRINT(" Total time = %d\r\n", chVTGetSystemTimeX() - total_time);
  DEBUG_PRINT(" CRC16 time %d\r\n", crc_time);
}
#endif

//*******************************************************
//               SD card SPI functions
//*******************************************************
bool SD_Inserted(void){
  return !(palReadPort(GPIOB)&(1<<GPIOB_SD_CD));
}

static void SD_Select_SPI(void) {
  set_SPI_mode(SPI_MODE_SD_CARD);
  SD_CS_LOW;                 // Select SD Card
}

static void SD_Select_SPI_LOW(void) {
  set_SPI_mode(SPI_MODE_SD_CARD_LOW);
  SD_CS_LOW;                 // Select SD Card
}

static void SD_Unselect_SPI(void) {
  SD_CS_HIGH;                         // Unselect SD Card
  spi_RxByte();                       // Dummy read/write one Byte recommend for SD after CS up
}

//*******************************************************
//*           SD functions
//*******************************************************
// CRC7 used for commands
#ifdef SD_USE_COMMAND_CRC
#define CRC7_POLY  0x89
#define CRC7_INIT  0x00
//                                        7   3
// CRC7 it's a 7 bit CRC with polynomial x + x + 1
static uint8_t crc7(const uint8_t *ptr, uint16_t count) {
  uint8_t crc = CRC7_INIT;
  uint8_t i = 0;
  while (count--){
    crc ^= *ptr++;
    do{
      if (crc & 0x80) crc^=CRC7_POLY;
      crc = crc << 1;
    } while((++i)&0x7);
  }
  return crc;
}
#endif
// CRC16 used for data
#ifdef SD_USE_DATA_CRC
#define CRC16_POLY  0x1021
#define CRC16_INIT  0x0000
//                                      16   12   5
// This is the CCITT CRC 16 polynomial X  + X  + X  + 1.
static uint16_t crc16(const uint8_t *ptr, uint16_t count) {
  uint16_t crc = CRC16_INIT;
#if DEBUG == 1
  crc_time-= chVTGetSystemTimeX();
#endif
  #if 0
  uint8_t i = 0;
  while(count--){
    crc^= ((uint16_t) *ptr++ << 8);
    do{
      if (crc & 0x8000)
        crc = (crc << 1) ^ CRC16_POLY;
      else
        crc = crc << 1;
    } while((++i)&0x7);
  }
  return __REVSH(crc); // swap bytes
  #else
  while (count--){
    crc^= *ptr++;
    crc^= (crc>> 4)&0x000F;
    crc^= (crc<<12);
    crc^= (crc<< 5)&0x1FE0;
    crc = __REVSH(crc); // swap bytes
  }
#if DEBUG == 1
  crc_time+= chVTGetSystemTimeX();
#endif
  return crc;
  #endif
}
#endif

// Wait and read R1 answer from SD
static inline uint8_t SD_ReadR1(uint32_t cnt) {
  uint8_t r1;
   // 8th bit R1 always zero, check it
  spi_DropRx();
  while(((r1=spi_RxByte())&0x80) && --cnt)
    ;
  return r1;
}

// Wait SD ready token answer (wait time in systick)
static inline bool SD_WaitDataToken(uint8_t token, uint32_t wait_time) {
  uint8_t res;
  uint32_t time = chVTGetSystemTimeX();
  uint8_t count = 0;
  spi_DropRx();
  do{
    if ((res = spi_RxByte()) == token)
      return true;
    count++;
    // Check timeout only every 256 bytes read (~8ms)
    if (count == 0 && (chVTGetSystemTimeX() - time) > wait_time)
      break;
  }while (res == 0xFF);
  return false;
}

static inline uint8_t SD_WaitDataAccept(uint32_t cnt) {
  uint8_t res;
  spi_DropRx();
  while ((res = spi_RxByte()) == 0xFF && --cnt)
    ;
  return res&0x1F;
}

// Wait no Busy answer from SD (wait time in systick)
static uint8_t SD_WaitNotBusy(uint32_t wait_time) {
  uint8_t res;
  uint32_t time = chVTGetSystemTimeX();
  uint8_t count = 0;
  spi_DropRx();
  do{
    if ((res = spi_RxByte()) == 0xFF)
      return res;
    count++;
    // Check timeout only every 256 bytes read (~8ms)
    if (count == 0 && (chVTGetSystemTimeX() - time) > wait_time)
      break;
  }while (1);
  return 0;
}

// Receive data block from SD
static bool SD_RxDataBlock(uint8_t *buff, uint16_t len, uint8_t token) {
  // loop until receive read response token or timeout ~50ms
  if (!SD_WaitDataToken(token, MS2ST(50))) {
    DEBUG_PRINT(" rx SD_WaitDataToken err\r\n");
    return FALSE;
  }
  // Receive data (Not use rx DMA)
#ifdef __USE_SDCARD_DMA_RX__
  spi_DMARxBuffer(buff, len, true);
#else
  spi_RxBuffer(buff, len);
#endif
  // Read and check CRC (if enabled)
  uint16_t crc; spi_RxBuffer((uint8_t*)&crc, 2);
#ifdef SD_USE_DATA_CRC
  uint16_t bcrc = crc16(buff, len);
  if (crc!=bcrc){
    DEBUG_PRINT("CRC = %04x , hcalc = %04x, calc = %04x\r\n", (uint32_t)crc, (uint32_t)bcrc, (uint32_t)crc16(buff, len));
    return FALSE;
  }
#endif
  return TRUE;
}

// Transmit data block to SD
static bool SD_TxDataBlock(const uint8_t *buff, uint16_t len, uint8_t token) {
  uint8_t resp;
  // Transmit token
  spi_TxByte(token);
#if 0         // Not use multiple block tx
  // if it's not STOP token, transmit data, in multiple block Tx
   if (token == SD_TOKEN_STOP_BLOCK) return TRUE;
#endif

#ifdef __USE_SDCARD_DMA__
  spi_DMATxBuffer((uint8_t*)buff, len, true);
#else
  spi_TxBuffer((uint8_t*)buff, len);
#endif
  // Send CRC
#ifdef  SD_USE_DATA_CRC
  uint16_t bcrc = crc16(buff, len);
  spi_TxWord(bcrc);
#else
  spi_TxWord(0xFFFF);
#endif
  // Receive transmit data response token on next 10 bytes
  resp = SD_WaitDataAccept(10);
  if (resp != SD_TOKEN_DATA_ACCEPTED){
    goto error_tx;
  }
#if 1
  // Wait busy (recommended timeout is 250ms (500ms for SDXC) set 250ms
  resp = SD_WaitNotBusy(MS2ST(250));
  if (resp == 0xFF)
    return TRUE;
#else
  // Continue execute, wait not busy on next command
  return TRUE;
#endif
  DEBUG_PRINT(" Tx busy error = %04\r\n", (uint32_t)resp);
  return FALSE;
error_tx:
  DEBUG_PRINT(" Tx accept error = %04x\r\n", (uint32_t)resp);
  return FALSE;
}

// Transmit command to SD
static uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg) {
  uint8_t buf[6];
  volatile uint8_t r1;
  // wait SD ready after last Tx (recommended timeout is 250ms (500ms for SDXC) set 250ms
  if ((r1 = SD_WaitNotBusy(MS2ST(500))) != 0xFF) {
    DEBUG_PRINT(" SD_WaitNotBusy CMD%d err, %02x\r\n", cmd-0x40, (uint32_t)r1);
    return 0xFF;
  }
  if (cmd != CMD24 && cmd != CMD17 )
    DEBUG_PRINT(" SD_SendCmd CMD%d, 0x%08x", (uint32_t)cmd-0x40, arg);

  // Transmit command
  buf[0] = cmd;
  buf[1] = (arg >> 24)&0xFF;
  buf[2] = (arg >> 16)&0xFF;
  buf[3] = (arg >>  8)&0xFF;
  buf[4] = (arg >>  0)&0xFF;
#ifdef SD_USE_COMMAND_CRC
  buf[5] = crc7(buf, 5)|0x01;
#else
  uint8_t crc = 0x01;              // Dummy CRC + Stop
       if (cmd == CMD0) crc = 0x95;// Valid CRC for CMD0(0)
  else if (cmd == CMD8) crc = 0x87;// Valid CRC for CMD8(0x1AA)
  buf[5] = crc;
#endif
  spi_TxBuffer(buf, 6);
// Skip a stuff byte when STOP_TRANSMISSION
//if (cmd == CMD12) SPI_RxByte();
  // Receive response register r1
   // 8th bit R1 always zero, check it
  spi_DropRx();
  int cnt = 100;
  while(((r1=spi_RxByte())&0x80) && --cnt) {
    if (cmd != CMD24 && cmd != CMD17 ) DEBUG_PRINT(" r1=0x%x", (uint32_t)r1);
  }
  if (cmd != CMD24 && cmd != CMD17 ) DEBUG_PRINT(" r1=0x%x\r\n", (uint32_t)r1);
//  r1 = SD_ReadR1(100);
#if 1
  if (r1&(SD_R1_NOT_R1|SD_R1_CRC_ERROR|SD_R1_ERASE_RESET|SD_R1_ERR_ERASE_CLR)){
    DEBUG_PRINT(" SD_SendCmd err CMD%d, 0x%x, 0x%08x\r\n", (uint32_t)cmd-0x40, (uint32_t)r1, arg);
    return r1;
  }
  if (r1&(~SD_R1_IDLE))
    DEBUG_PRINT(" SD_SendCmd CMD%d, r1=0x%x\r\n", (uint32_t)cmd-0x40, (uint32_t)r1);
#endif
  return r1;
}

// Power on SD
static void SD_PowerOn(void) {
  uint16_t n;
  LCD_CS_HIGH;
  // Dummy TxRx 80 bits for power up SD
  for (n=0;n<10;n++)
    spi_RxByte();
  SD_Select_SPI_LOW();
  // Set SD card to idle state
  if (SD_SendCmd(CMD0, 0) == SD_R1_IDLE)
    Stat|= STA_POWER_ON;
  SD_Unselect_SPI();
}

// Power off SD
void SD_PowerOff(void) {
  Stat &= ~STA_POWER_ON;
}

// Check power flag
static inline uint8_t SD_CheckPower(void) {
  return Stat & STA_POWER_ON;
}

//*******************************************************
//       diskio.c functions for file system library
//*******************************************************
// If enable RTC - get RTC time
#if FF_FS_NORTC == 0
DWORD get_fattime (void) {
  return rtc_get_FAT();
}
#endif

// diskio.c - Initialize SD
DSTATUS disk_initialize(BYTE pdrv) {
  if (CardType && (Stat & STA_POWER_ON))
    return 0;
// Debug counters
#if DEBUG == 1
  w_cnt = 0;
  w_time = 0;
  r_cnt = 0;
  r_time = 0;
  crc_time = 0;
  total_time = chVTGetSystemTimeX();
#endif
  if (pdrv != 0) return STA_NOINIT;
  // Start init SD card
  Stat = STA_NOINIT;
  // power on, try detect on bus, set card to idle state
  SD_PowerOn();
  if (!SD_CheckPower()) return Stat;
  // check disk type
  uint8_t  type = 0;
  uint32_t cnt = 100;
  // Set low SPI bus speed = PLL/256 (on 72MHz =281.250kHz)
  SD_Select_SPI_LOW();
  // send GO_IDLE_STATE command
  if (SD_SendCmd(CMD0, 0) == SD_R1_IDLE)
  {
    DEBUG_PRINT(" CMD0 Ok\r\n");
    // SDC V2+ accept CMD8 command, http://elm-chan.org/docs/mmc/mmc_e.html
    if (SD_SendCmd(CMD8, 0x00001AAU) == SD_R1_IDLE)
    {
      DEBUG_PRINT(" CMD8 Ok\r\n");
      uint32_t ocr; spi_RxBuffer((uint8_t *)&ocr, 4);
      DEBUG_PRINT(" CMD8 0x%x\r\n", ocr);
      // operation condition register voltage range 2.7-3.6V
      if (ocr == _OCR(0x00001AAU))
      {
        // ACMD41 with HCS bit can be up to 200ms wait
        do {
          if ((SD_SendCmd(CMD55,                0) <= 1) && // APP_CMD Get Ok or idle state
              (SD_SendCmd(ACMD41, SD_OCR_CAPACITY) == 0))   // Check OCR
            break;
          chThdSleepMilliseconds(10);
        } while (--cnt);
        DEBUG_PRINT(" CMD55 + ACMD41 %d\r\n", cnt);
        // READ_OCR
        if (cnt && SD_SendCmd(CMD58, 0) == 0)
        {
          DWORD ocr; spi_RxBuffer((uint8_t *)&ocr, 4);
          DEBUG_PRINT(" CMD58 OCR = 0x%08x\r\n", _OCR(ocr));
          // Check CCS bit, SDv2 (HC or SC)
          type = (ocr & _OCR(SD_OCR_CAPACITY)) ? CT_SD2 | CT_BLOCK : CT_SD2;
        }
      }
#ifdef SD_USE_COMMAND_CRC
#ifdef SD_USE_DATA_CRC
      SD_SendCmd(CMD59, 1); // Enable CRC check on card
#endif
#endif
//      uint8_t csd[16];
//      if (SD_SendCmd(CMD9, 0) == 0 && SD_RxDataBlock(csd, 16, SD_TOKEN_START_BLOCK)){
//        DEBUG_PRINT(" CSD =");
//        for (int i = 0; i<16; i++)
//          DEBUG_PRINT(" %02X", csd[i]);
//        DEBUG_PRINT("\r\n");
//      }
    } else {
      DEBUG_PRINT(" CMD8 Fail\r\n");
      // SDC V1 or MMC
      type = (SD_SendCmd(CMD55,  0) <= 1 &&                  // APP_CMD
              SD_SendCmd(ACMD41, 0) <= 1) ? CT_SD1 : CT_MMC;
      DEBUG_PRINT(" CMD55 %d\r\n", type);
      do{
        if (type == CT_SD1)
        {
          if (SD_SendCmd(CMD55, 0) <= 1 && SD_SendCmd(ACMD41, 0) == 0) break; // ACMD41
        }
        else if (SD_SendCmd(CMD1, 0) == 0) break; // CMD1
        chThdSleepMilliseconds(10);
      } while (--cnt);
      // SET_BLOCKLEN
      if (!cnt || SD_SendCmd(CMD16, SD_SECTOR_SIZE) != 0) type = 0;
      DEBUG_PRINT(" CMD16 %d %d\r\n", cnt, type);
    }
  }
  SD_Unselect_SPI();
  CardType = type;
  DEBUG_PRINT("CardType %d\r\n", type);
  // Clear STA_NOINIT
  if (type)
    Stat&=~STA_NOINIT;
  else // Initialization failed
    SD_PowerOff();
  return Stat;
}

// diskio.c - Return disk status
DSTATUS disk_status(BYTE pdrv) {
  if (pdrv != 0) return STA_NOINIT;
  return Stat;
}

// diskio.c - Read sector
DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
  // No disk or wrong block count
  if (pdrv != 0 || (Stat & STA_NOINIT)) return RES_NOTRDY;
  // convert to byte address
  if (!(CardType & CT_BLOCK)) sector *= SD_SECTOR_SIZE;

#if DEBUG == 1
  r_cnt++;
  r_time-= chVTGetSystemTimeX();
#endif

  SD_Select_SPI();
  uint8_t cmd = count == 1 ? CMD17 : CMD18;
    // convert to byte address
  if (!(CardType & CT_BLOCK)) sector*= SD_SECTOR_SIZE;
  // Read single / multiple block
  if (SD_SendCmd(cmd, sector) == 0) {
    do {
      if (SD_RxDataBlock(buff, SD_SECTOR_SIZE, SD_TOKEN_START_BLOCK))
        buff+= SD_SECTOR_SIZE;
      else break;
    } while(--count);
  }
  if (cmd == CMD18) SD_SendCmd(CMD12, 0);  // Finish multiple block transfer
  SD_Unselect_SPI();

#if DEBUG == 1
  r_time+= chVTGetSystemTimeX();
  if (count)
    DEBUG_PRINT(" err READ_BLOCK %d 0x%08x\r\n", count, sector);
#if 0
  else{
    DEBUG_PRINT("Sector read 0x%08x %d \r\n", sector, cnt);
    for (UINT j = 0; j < 32; j++){
      for (UINT i = 0; i < 16; i++)
        DEBUG_PRINT(" 0x%02x", buff[j*16 + i]);
      DEBUG_PRINT("\r\n");
    }
  }
#endif
#endif

  return count ? RES_ERROR : RES_OK;
}

// diskio.c - Write sector
DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
  // No disk or wrong count
  if (pdrv != 0 || (Stat & STA_NOINIT)) return RES_NOTRDY;
  // Write protection
  if (Stat & STA_PROTECT) return RES_WRPRT;
  #if DEBUG == 1
#if 0
    DEBUG_PRINT("Sector write 0x%08X, %d\r\n", sector, count);
    for (UINT j = 0; j < 32; j++){
      for (UINT i = 0; i < 16; i++)
        DEBUG_PRINT(" 0x%02X", buff[j*16 + i]);
      DEBUG_PRINT("\r\n");
    }
#endif
  w_cnt++;
  w_time-= chVTGetSystemTimeX();
#endif

  SD_Select_SPI();
  do {
    // WRITE_SINGLE_BLOCK * count
    uint32_t sect = (CardType & CT_BLOCK) ? sector : sector * SD_SECTOR_SIZE;
    if ((SD_SendCmd(CMD24, sect) == 0) && SD_TxDataBlock(buff, SD_SECTOR_SIZE, SD_TOKEN_START_BLOCK)) {
      sector++;
      buff+= SD_SECTOR_SIZE;
    } else break;
  } while (--count);
  SD_Unselect_SPI();

#if DEBUG == 1
  w_time+= chVTGetSystemTimeX();
  if (count)
    DEBUG_PRINT(" WRITE_BLOCK %d 0x%08X\r\n", count, sector);
#endif

  return count ? RES_ERROR : RES_OK;
}

// The disk_ioctl function is called to control device specific features and miscellaneous functions other than generic read/write.
// Implement only five device independent commands used by FatFS module
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  (void)buff;
  DRESULT res = RES_PARERR;
  // No disk or not ready
  if (pdrv != 0 || Stat & STA_NOINIT) return RES_NOTRDY;
  SD_Select_SPI();
  switch (cmd){
    // Makes sure that the device has finished pending write process.
    // If the disk I/O layer or storage device has a write-back cache,
    // the dirty cache data must be committed to media immediately.
    // Nothing to do for this command if each write operation to the media is completed
    // within the disk_write function.
    case CTRL_SYNC:
      if (SD_WaitNotBusy(MS2ST(200)) == 0xFF) res = RES_OK;
    break;
#if FF_USE_TRIM == 1
    // Informs the device the data on the block of sectors is no longer needed and it can be erased.
    // The sector block is specified in an LBA_t array {<Start LBA>, <End LBA>} pointed by buff.
    // This is an identical command to Trim of ATA device. Nothing to do for this command if this function
    // is not supported or not a flash memory device. FatFs does not check the result code and the file function
    // is not affected even if the sector block was not erased well. This command is called on remove a cluster chain
    // and in the f_mkfs function. It is required when FF_USE_TRIM == 1.
    case CTRL_TRIM:
    break;
#endif
#if FF_MAX_SS > FF_MIN_SS
    // Retrieves sector size used for read/write function into the WORD variable pointed by buff.
    // Valid sector sizes are 512, 1024, 2048 and 4096. This command is required only if FF_MAX_SS > FF_MIN_SS.
    // When FF_MAX_SS == FF_MIN_SS, this command will be never used and the read/write function must work in FF_MAX_SS bytes/sector only.
    case GET_SECTOR_SIZE:
      *(uint16_t*) buff = SD_SECTOR_SIZE;
      res = RES_OK;
    break;
#endif
#if FF_USE_MKFS == 1
    // Retrieves erase block size of the flash memory media in unit of sector into the DWORD variable pointed by buff.
    // The allowable value is 1 to 32768 in power of 2. Return 1 if the erase block size is unknown or non flash memory media.
    // This command is used by only f_mkfs function and it attempts to align data area on the erase block boundary.
    // It is required when FF_USE_MKFS == 1.
    case GET_BLOCK_SIZE:
   	 *(uint16_t*) buff = ;//SD_SECTOR_SIZE;
      res = RES_OK;
    break;
    // Retrieves number of available sectors, the largest allowable LBA + 1, on the drive into the LBA_t variable pointed by buff.
    // This command is used by f_mkfs and f_fdisk function to determine the size of volume/partition to be created.
    // It is required when FF_USE_MKFS == 1.
    case GET_SECTOR_COUNT:
    {
      // SEND_CSD
      uint8_t csd[16];
      if ((SD_SendCmd(CMD9, 0) == 0) && SD_RxDataBlock(csd, 16, SD_TOKEN_START_BLOCK)) {
        uint32_t n, csize;
        if ((csd[0] >> 6) == 1)  {  // SDC V2
          csize = ((uint32_t)csd[7]<<16)|((uint32_t)csd[8]<< 8)|((uint32_t)csd[9]<< 0);
          n = 10;
        }
        else {                      // MMC or SDC V1
          csize = ((uint32_t)csd[8]>>6)|((uint32_t)csd[7]<<2)|((uint32_t)(csd[6]&0x03)<<10);
          n = ((csd[5]&0x0F)|((csd[10]&0x80)>>7)|((csd[9]&0x03)<<1)) + 2 - 9;
        }
        *(uint32_t*)buff = (csize+1)<<n;
        res = RES_OK;
      }
    }
    break;
#endif
  }
  SD_Unselect_SPI();
  DEBUG_PRINT("disk_ioctl(%d) = %d,\r\n", cmd, res);
#if DEBUG == 1
  testLog();
#endif
  return res;
}
#endif //__USE_SD_CARD__
