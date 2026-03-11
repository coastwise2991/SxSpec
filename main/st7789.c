#include "st7789.h"
#include "board_config.h"
#include "font5x7.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ST7789";
static spi_device_handle_t lcd_spi = NULL;

// -------------------------------------------------------
// SPI send helpers
// -------------------------------------------------------

static void lcd_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_PIN_DC, 0);
    spi_transaction_t t = {.length = 8, .tx_data = {cmd}, .flags = SPI_TRANS_USE_TXDATA};
    spi_device_polling_transmit(lcd_spi, &t);
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if (!len) return;
    gpio_set_level(LCD_PIN_DC, 1);
    spi_transaction_t t = {0};
    t.length    = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(lcd_spi, &t);
}

static void lcd_data_byte(uint8_t d)
{
    lcd_data(&d, 1);
}

// -------------------------------------------------------
// Initialization
// -------------------------------------------------------

esp_err_t st7789_init(void)
{
    ESP_LOGI(TAG, "Init ST7789V2 240x135");

    // Config GPIO
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST) | (1ULL << LCD_PIN_BL),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    // SPI bus already initialized in main; just add device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = LCD_PIN_CS,
        .queue_size     = 7,
        .pre_cb         = NULL,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev_cfg, &lcd_spi));

    // Hardware reset
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Init sequence
    lcd_cmd(ST7789_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    lcd_cmd(ST7789_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(255));

    lcd_cmd(ST7789_COLMOD); lcd_data_byte(0x55); // 16-bit RGB565
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_cmd(ST7789_MADCTL); lcd_data_byte(0x70); // Landscape, BGR

    lcd_cmd(ST7789_PORCTRL);
    lcd_data((uint8_t[]){0x0C,0x0C,0x00,0x33,0x33}, 5);

    lcd_cmd(ST7789_GCTRL); lcd_data_byte(0x35);

    lcd_cmd(ST7789_VCOMS); lcd_data_byte(0x19);

    lcd_cmd(ST7789_LCMCTRL); lcd_data_byte(0x2C);

    lcd_cmd(ST7789_VDVVRHEN); lcd_data_byte(0x01);
    lcd_cmd(ST7789_VRHS);     lcd_data_byte(0x12);
    lcd_cmd(ST7789_VDVS);     lcd_data_byte(0x20);

    lcd_cmd(ST7789_FRCTRL2); lcd_data_byte(0x0F); // 60Hz

    lcd_cmd(ST7789_PWCTRL1);
    lcd_data((uint8_t[]){0xA4, 0xA1}, 2);

    lcd_cmd(ST7789_PVGAMCTRL);
    lcd_data((uint8_t[]){0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,
                          0x4C,0x18,0x0D,0x0B,0x1F,0x23}, 14);
    lcd_cmd(ST7789_NVGAMCTRL);
    lcd_data((uint8_t[]){0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,
                          0x51,0x2F,0x1F,0x1F,0x20,0x23}, 14);

    lcd_cmd(ST7789_INVON);
    lcd_cmd(ST7789_NORON);
    lcd_cmd(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Turn on backlight
    gpio_set_level(LCD_PIN_BL, 1);

    // Clear screen black
    st7789_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);

    ESP_LOGI(TAG, "ST7789 ready");
    return ESP_OK;
}

// -------------------------------------------------------
// Drawing primitives
// -------------------------------------------------------

void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Cardputer 1.14" has offsets: x+40, y+53 for 240x135 on a 240x320 driver
    x0 += 40; x1 += 40;
    y0 += 53; y1 += 53;

    lcd_cmd(ST7789_CASET);
    lcd_data((uint8_t[]){x0>>8, x0&0xFF, x1>>8, x1&0xFF}, 4);
    lcd_cmd(ST7789_RASET);
    lcd_data((uint8_t[]){y0>>8, y0&0xFF, y1>>8, y1&0xFF}, 4);
    lcd_cmd(ST7789_RAMWR);
    gpio_set_level(LCD_PIN_DC, 1);
}

void st7789_push_pixels(uint16_t *pixels, uint32_t count)
{
    // Send as big-endian RGB565
    uint16_t *swapped = malloc(count * 2);
    if (!swapped) return;
    for (uint32_t i = 0; i < count; i++) {
        swapped[i] = (pixels[i] >> 8) | (pixels[i] << 8);
    }
    gpio_set_level(LCD_PIN_DC, 1);
    spi_transaction_t t = {0};
    t.length    = count * 16;
    t.tx_buffer = swapped;
    spi_device_polling_transmit(lcd_spi, &t);
    free(swapped);
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    st7789_set_window(x, y, x + w - 1, y + h - 1);
    uint32_t total = (uint32_t)w * h;
    uint16_t *buf = malloc(total * 2);
    if (!buf) return;
    uint16_t c = (color >> 8) | (color << 8);
    for (uint32_t i = 0; i < total; i++) buf[i] = c;
    gpio_set_level(LCD_PIN_DC, 1);
    spi_transaction_t t = {0};
    t.length    = total * 16;
    t.tx_buffer = buf;
    spi_device_polling_transmit(lcd_spi, &t);
    free(buf);
}

void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    st7789_set_window(x, y, x, y);
    uint8_t d[2] = {color >> 8, color & 0xFF};
    lcd_data(d, 2);
}

void st7789_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t *colors)
{
    st7789_set_window(x, y, x + w - 1, y);
    st7789_push_pixels(colors, w);
}

// -------------------------------------------------------
// Simple 5x7 font text rendering
// -------------------------------------------------------

void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg)
{
    while (*str) {
        uint8_t c = *str++;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = font5x7[c - 32];
        for (uint8_t col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (uint8_t row = 0; row < 7; row++) {
                uint16_t color = (bits & (1 << row)) ? fg : bg;
                st7789_draw_pixel(x + col, y + row, color);
            }
        }
        x += 6;
    }
}

void st7789_backlight(uint8_t on)
{
    gpio_set_level(LCD_PIN_BL, on ? 1 : 0);
}
