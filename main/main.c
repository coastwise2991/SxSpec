#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "sx1262.h"
#include "st7789.h"
#include "waterfall.h"

static const char *TAG = "MAIN";

// -------------------------------------------------------
// Global state
// -------------------------------------------------------

static sx1262_t radio = {0};
static uint32_t g_center_hz  = RF_DEFAULT_CTR_HZ;
static uint32_t g_span_hz    = RF_SPAN_HZ;
static int8_t   g_rssi_floor = -120;  // dBm floor for normalization
static int8_t   g_rssi_ceil  = -40;   // dBm ceiling for normalization

// -------------------------------------------------------
// Keyboard — TCA8418 I2C controller (Cardputer ADV)
// -------------------------------------------------------

// TCA8418 key code -> ASCII map (Cardputer ADV physical layout)
static const char tca8418_keymap[0x60] = {
    0,    0,    0,    0,    0,    0,    0,    0,    // 0x00-0x07
    0,    0,    0,    0,    0,    0,    0,    0,    // 0x08-0x0F
   '`',  '1',  '2',  '3',  '4',  '5',  '6',  '7', // 0x10-0x17
   '8',  '9',  '0',  '-',  '=',  0,    0,    'q',  // 0x18-0x1F
   'w',  'e',  'r',  't',  'y',  'u',  'i',  'o',  // 0x20-0x27
   'p',  '[',  ']', '\\',  0,    'a',  's',  'd',  // 0x28-0x2F
   'f',  'g',  'h',  'j',  'k',  'l',  ';', '\'',  // 0x30-0x37
    0,    0,    'z',  'x',  'c',  'v',  'b',  'n',  // 0x38-0x3F
   'm',  ',',  '.',  '/',  0,    ' ',  0,    0,    // 0x40-0x47
    0,    0,    0,    0,    0,    0,    0,    0,    // 0x48-0x4F
    '+',  '-',  0,    0,    0,    0,    0,    0,    // 0x50-0x57
    0,    0,    0,    0,    0,    0,    0,    0,    // 0x58-0x5F
};

static esp_err_t tca8418_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(KB_I2C_PORT, KB_I2C_ADDR,
                                      buf, 2, pdMS_TO_TICKS(10));
}

static esp_err_t tca8418_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(KB_I2C_PORT, KB_I2C_ADDR,
                                        &reg, 1, val, 1,
                                        pdMS_TO_TICKS(10));
}

static esp_err_t kb_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = KB_PIN_SDA,
        .scl_io_num       = KB_PIN_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t r = i2c_param_config(KB_I2C_PORT, &conf);
    if (r != ESP_OK) return r;
    r = i2c_driver_install(KB_I2C_PORT, conf.mode, 0, 0, 0);
    if (r != ESP_OK) return r;

    // Enable TCA8418 key event FIFO + interrupt
    r = tca8418_write_reg(TCA8418_REG_CFG, TCA8418_CFG_KE_IEN | TCA8418_CFG_AI);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "TCA8418 not found at 0x%02X", KB_I2C_ADDR);
        return r;
    }

    // INT pin: input with pull-up, falling edge
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << KB_PIN_INT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&int_cfg);

    ESP_LOGI(TAG, "TCA8418 keyboard ready");
    return ESP_OK;
}

static char kb_read_key(void)
{
    // INT line high = no events
    if (gpio_get_level(KB_PIN_INT) != 0) return 0;

    uint8_t key_count = 0;
    tca8418_read_reg(TCA8418_REG_KEY_LCK_EC, &key_count);
    key_count &= 0x0F;
    if (!key_count) return 0;

    uint8_t key_event = 0;
    tca8418_read_reg(TCA8418_REG_KEY_EVENT_A, &key_event);

    // Bit 7: 1=press 0=release
    bool pressed   = (key_event & 0x80) != 0;
    uint8_t kc     = key_event & 0x7F;

    // Clear interrupt
    uint8_t int_stat = 0;
    tca8418_read_reg(TCA8418_REG_INT_STAT, &int_stat);
    tca8418_write_reg(TCA8418_REG_INT_STAT, int_stat);

    if (!pressed) return 0;
    if (kc >= sizeof(tca8418_keymap)) return 0;
    return tca8418_keymap[kc];
}

// -------------------------------------------------------
// RSSI normalization: maps dBm to 0-255
// -------------------------------------------------------

static uint8_t normalize_rssi(int8_t rssi_dbm)
{
    if (rssi_dbm <= g_rssi_floor) return 0;
    if (rssi_dbm >= g_rssi_ceil)  return 255;
    int range = g_rssi_ceil - g_rssi_floor;
    int val   = rssi_dbm - g_rssi_floor;
    return (uint8_t)((val * 255) / range);
}

// -------------------------------------------------------
// RF Scan: sweep RF_SCAN_POINTS frequencies across span
// -------------------------------------------------------

static void do_rf_scan(uint8_t *out_row)
{
    uint32_t start_hz = g_center_hz - (g_span_hz / 2);

    for (int i = 0; i < RF_SCAN_POINTS; i++) {
        uint32_t freq = start_hz + (uint32_t)i * RF_STEP_HZ;

        if (freq < RF_MIN_FREQ_HZ) freq = RF_MIN_FREQ_HZ;
        if (freq > RF_MAX_FREQ_HZ) freq = RF_MAX_FREQ_HZ;

        sx1262_set_frequency(&radio, freq);
        sx1262_write_cmd(&radio, SX1262_CMD_SET_FS, NULL, 0);
        esp_rom_delay_us(200);

        uint8_t rx_timeout[3] = {0x00, 0x00, 0x40};
        sx1262_write_cmd(&radio, SX1262_CMD_SET_RX, rx_timeout, 3);
        esp_rom_delay_us(500);

        int8_t rssi    = sx1262_get_rssi_inst(&radio);
        out_row[i]     = normalize_rssi(rssi);

        sx1262_set_standby(&radio, SX1262_STANDBY_XOSC);
    }
}

// -------------------------------------------------------
// Handle keyboard input for tuning
// -------------------------------------------------------

static void handle_key(char key)
{
    uint32_t step = 250000UL;

    switch (key) {
        case 'a': case ',':
            if (g_center_hz - step >= RF_MIN_FREQ_HZ + g_span_hz/2)
                g_center_hz -= step;
            break;
        case 'd': case '.':
            if (g_center_hz + step <= RF_MAX_FREQ_HZ - g_span_hz/2)
                g_center_hz += step;
            break;
        case 'w':
            if (g_center_hz + 1000000UL <= RF_MAX_FREQ_HZ - g_span_hz/2)
                g_center_hz += 1000000UL;
            break;
        case 's':
            if (g_center_hz - 1000000UL >= RF_MIN_FREQ_HZ + g_span_hz/2)
                g_center_hz -= 1000000UL;
            break;
        case '+':
            if (g_rssi_ceil < -20) g_rssi_ceil += 5;
            break;
        case '-':
            if (g_rssi_ceil > g_rssi_floor + 20) g_rssi_ceil -= 5;
            break;
        default:
            return;
    }

    waterfall_draw_header(g_center_hz, g_span_hz);
    ESP_LOGI(TAG, "Center: %lu MHz  Floor:%ddBm Ceil:%ddBm",
             g_center_hz / 1000000, g_rssi_floor, g_rssi_ceil);
}

// -------------------------------------------------------
// SPI bus init — TWO separate buses:
//   SPI2 = LCD (MOSI:35, CLK:36, no MISO needed)
//   SPI3 = SX1262 (MOSI:14, MISO:39, CLK:40)
// -------------------------------------------------------

static void spi_bus_init(void)
{
    // LCD bus (SPI2) — write-only, no MISO
    spi_bus_config_t lcd_bus = {
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &lcd_bus, SPI_DMA_CH_AUTO));

    // Radio bus (SPI3) — full duplex for SX1262
    spi_bus_config_t radio_bus = {
        .mosi_io_num     = SX1262_PIN_MOSI,
        .miso_io_num     = SX1262_PIN_MISO,
        .sclk_io_num     = SX1262_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 256,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &radio_bus, SPI_DMA_CH_AUTO));
}

// -------------------------------------------------------
// SX1262 GPIO & SPI device setup
// -------------------------------------------------------

static void radio_gpio_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << SX1262_PIN_NSS) | (1ULL << SX1262_PIN_RESET),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(SX1262_PIN_NSS, 1);
    gpio_set_level(SX1262_PIN_RESET, 1);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << SX1262_PIN_BUSY) | (1ULL << SX1262_PIN_DIO1),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&in);
}

static void radio_spi_init(void)
{
    spi_device_interface_config_t dev = {
        .clock_speed_hz = SX1262_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = -1,  // Manual CS via GPIO
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SX1262_SPI_HOST, &dev, &radio.spi));
    radio.pin_nss   = SX1262_PIN_NSS;
    radio.pin_reset = SX1262_PIN_RESET;
    radio.pin_busy  = SX1262_PIN_BUSY;
    radio.pin_dio1  = SX1262_PIN_DIO1;
}

// -------------------------------------------------------
// Main
// -------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "RF Waterfall Spectrogram");
    ESP_LOGI(TAG, "Board: Cardputer ADV  Radio: Cap LoRa-1262 (SX1262)");
    ESP_LOGI(TAG, "Span: %.1f MHz | Range: 862-928 MHz",
             (float)RF_SPAN_HZ / 1e6);

    // 1. Init dual SPI buses (LCD on SPI2, SX1262 on SPI3)
    spi_bus_init();

    // 2. Init LCD
    ESP_ERROR_CHECK(st7789_init());

    // 3. Init radio
    radio_gpio_init();
    radio_spi_init();
    ESP_ERROR_CHECK(sx1262_init(&radio));
    ESP_ERROR_CHECK(sx1262_prepare_for_scan(&radio));

    // 4. Init keyboard (TCA8418)
    if (kb_init() != ESP_OK) {
        ESP_LOGW(TAG, "Keyboard init failed (non-fatal, continuing)");
    }

    // 5. Init waterfall
    waterfall_init();
    waterfall_draw_header(g_center_hz, g_span_hz);
    waterfall_render();

    st7789_write_string(4, HEADER_HEIGHT + 4,
                        "Scanning 862-928MHz...", COLOR_YELLOW, COLOR_BLACK);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Scan loop start");

    static uint8_t scan_row[RF_SCAN_POINTS];
    int full_redraw_cnt = 0;

    while (1) {
        char key = kb_read_key();
        if (key) handle_key(key);

        do_rf_scan(scan_row);

        waterfall_push_row(scan_row, RF_SCAN_POINTS);
        waterfall_render_top_row();

        if (++full_redraw_cnt >= 30) {
            full_redraw_cnt = 0;
            waterfall_render();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
