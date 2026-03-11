#pragma once

// ============================================================
//  Board:   M5Stack Cardputer ADV (ESP32-S3)
//  Radio:   Cap LoRa-1262 (SX1262, via back expansion header)
//  Display: ST7789V2 1.14" 240x135 (built-in SPI LCD)
// ============================================================

// ----- ST7789V2 Display SPI (unchanged from original Cardputer) -----
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_MOSI        35
#define LCD_PIN_CLK         36
#define LCD_PIN_CS          37
#define LCD_PIN_DC          34
#define LCD_PIN_RST         33
#define LCD_PIN_BL          38       // Backlight (active high)
#define LCD_WIDTH           240
#define LCD_HEIGHT          135
#define LCD_SPI_FREQ_HZ     (40 * 1000 * 1000)

// ----- SX1262 SPI (Cap LoRa-1262 verified pinout) -----
// Uses dedicated SPI3 host to avoid conflict with LCD on SPI2
#define SX1262_SPI_HOST     SPI3_HOST
#define SX1262_PIN_MOSI     14        // LoRa_MOSI
#define SX1262_PIN_MISO     39        // LoRa_MISO
#define SX1262_PIN_SCK      40        // LoRa_SCK
#define SX1262_PIN_NSS      5         // LoRa_NSS
#define SX1262_PIN_RESET    3         // LoRa_RST
#define SX1262_PIN_BUSY     6         // LoRa_BUSY
#define SX1262_PIN_DIO1     4         // LoRa_IRQ
#define SX1262_SPI_FREQ_HZ  (8 * 1000 * 1000)

// ----- Keyboard (Cardputer ADV uses TCA8418 I2C controller) -----
// NOTE: G8/G9 are used by keyboard I2C — do NOT reassign
#define KB_I2C_PORT         I2C_NUM_0
#define KB_PIN_SDA          8         // Shared with Cap-Bus GPS_TX line
#define KB_PIN_SCL          9         // Shared with Cap-Bus GPS_RX line
#define KB_PIN_INT          11        // TCA8418 interrupt (falling edge)
#define KB_I2C_ADDR         0x34      // TCA8418 default I2C address
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LCK_EC  0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_CFG_KE_IEN      0x01  // Key events interrupt enable
#define TCA8418_CFG_AI          0x80  // Auto-increment

// ----- RF Scan Parameters -----
#define RF_MIN_FREQ_HZ      862000000UL   // 862 MHz
#define RF_MAX_FREQ_HZ      928000000UL   // 928 MHz
#define RF_DEFAULT_CTR_HZ   895000000UL   // 895 MHz default center
#define RF_SPAN_HZ          5000000UL     // 5 MHz span
#define RF_SCAN_POINTS      240           // One sample per LCD column
#define RF_STEP_HZ          (RF_SPAN_HZ / RF_SCAN_POINTS)  // ~20.8 kHz/step

// ----- Waterfall Display Layout -----
#define HEADER_HEIGHT       16    // Top bar: freq + span info
#define WATERFALL_HEIGHT    (LCD_HEIGHT - HEADER_HEIGHT)  // 119 rows of history
#define WATERFALL_WIDTH     LCD_WIDTH   // 240 columns = 240 scan points

// ----- SX1262 Hardware Config -----
#define SX1262_TCXO_MV      1800   // 1.8V TCXO
#define SX1262_USE_DCDC     1
