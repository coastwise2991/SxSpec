#pragma once

#include "esp_err.h"
#include <stdint.h>

// -------------------------------------------------------
// ST7789 Command opcodes
// -------------------------------------------------------
#define ST7789_SWRESET      0x01
#define ST7789_SLPOUT       0x11
#define ST7789_NORON        0x13
#define ST7789_INVON        0x21
#define ST7789_DISPON       0x29
#define ST7789_CASET        0x2A
#define ST7789_RASET        0x2B
#define ST7789_RAMWR        0x2C
#define ST7789_MADCTL       0x36
#define ST7789_COLMOD       0x3A
#define ST7789_PORCTRL      0xB2
#define ST7789_GCTRL        0xB7
#define ST7789_VCOMS        0xBB
#define ST7789_LCMCTRL      0xC0
#define ST7789_VDVVRHEN     0xC2
#define ST7789_VRHS         0xC3
#define ST7789_VDVS         0xC4
#define ST7789_FRCTRL2      0xC6
#define ST7789_PWCTRL1      0xD0
#define ST7789_PVGAMCTRL    0xE0
#define ST7789_NVGAMCTRL    0xE1

// -------------------------------------------------------
// RGB565 color helpers
// -------------------------------------------------------
#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))

#define COLOR_BLACK     RGB565(0,   0,   0)
#define COLOR_WHITE     RGB565(255, 255, 255)
#define COLOR_RED       RGB565(255, 0,   0)
#define COLOR_GREEN     RGB565(0,   255, 0)
#define COLOR_BLUE      RGB565(0,   0,   255)
#define COLOR_CYAN      RGB565(0,   255, 255)
#define COLOR_YELLOW    RGB565(255, 255, 0)
#define COLOR_MAGENTA   RGB565(255, 0,   255)

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

/**
 * @brief Initialize the ST7789 LCD and SPI device.
 *        Assumes the SPI2 bus has already been initialized.
 */
esp_err_t st7789_init(void);

/**
 * @brief Set the pixel write window.
 */
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief Push raw RGB565 pixels into the current window.
 */
void st7789_push_pixels(uint16_t *pixels, uint32_t count);

/**
 * @brief Fill a rectangle with a solid color.
 */
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Draw a single pixel.
 */
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief Draw a horizontal line using a color array.
 */
void st7789_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t *colors);

/**
 * @brief Render a string using the built-in 5x7 font.
 * @param fg Foreground color (RGB565)
 * @param bg Background color (RGB565)
 */
void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg);

/**
 * @brief Turn the backlight on or off.
 * @param on 1 = on, 0 = off
 */
void st7789_backlight(uint8_t on);
