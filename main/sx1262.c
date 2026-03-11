#include "sx1262.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SX1262";

// -------------------------------------------------------
// Low-level SPI helpers
// -------------------------------------------------------

void sx1262_wait_busy(sx1262_t *dev)
{
    uint32_t timeout = 10000;
    while (gpio_get_level(dev->pin_busy) && --timeout) {
        esp_rom_delay_us(10);
    }
    if (!timeout) {
        ESP_LOGW(TAG, "BUSY timeout");
    }
}

esp_err_t sx1262_write_cmd(sx1262_t *dev, uint8_t cmd, const uint8_t *data, size_t len)
{
    sx1262_wait_busy(dev);

    spi_transaction_t t = {0};
    uint8_t buf[len + 1];
    buf[0] = cmd;
    if (data && len) memcpy(&buf[1], data, len);

    t.length    = (len + 1) * 8;
    t.tx_buffer = buf;
    t.user      = (void *)0;  // DC=0

    gpio_set_level(dev->pin_nss, 0);
    esp_err_t ret = spi_device_polling_transmit(dev->spi, &t);
    gpio_set_level(dev->pin_nss, 1);
    return ret;
}

esp_err_t sx1262_read_cmd(sx1262_t *dev, uint8_t cmd, uint8_t *buf, size_t len)
{
    sx1262_wait_busy(dev);

    // SX1262 read: cmd + NOP status byte + N data bytes
    size_t total = len + 2;
    uint8_t tx[total];
    uint8_t rx[total];
    memset(tx, 0x00, total);
    tx[0] = cmd;

    spi_transaction_t t = {0};
    t.length    = total * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    gpio_set_level(dev->pin_nss, 0);
    esp_err_t ret = spi_device_polling_transmit(dev->spi, &t);
    gpio_set_level(dev->pin_nss, 1);

    if (ret == ESP_OK && buf) {
        memcpy(buf, &rx[2], len);
    }
    return ret;
}

static esp_err_t sx1262_write_register(sx1262_t *dev, uint16_t addr, uint8_t value)
{
    uint8_t data[3] = {(addr >> 8) & 0xFF, addr & 0xFF, value};
    return sx1262_write_cmd(dev, SX1262_CMD_WRITE_REGISTER, data, 3);
}

// -------------------------------------------------------
// Mid-level commands
// -------------------------------------------------------

esp_err_t sx1262_set_standby(sx1262_t *dev, uint8_t mode)
{
    return sx1262_write_cmd(dev, SX1262_CMD_SET_STANDBY, &mode, 1);
}

esp_err_t sx1262_set_frequency(sx1262_t *dev, uint32_t freq_hz)
{
    // PLL step = 32e6 / 2^25 = 0.953674316 Hz
    uint32_t frf = (uint32_t)((double)freq_hz / 0.95367431640625);
    uint8_t buf[4] = {
        (frf >> 24) & 0xFF,
        (frf >> 16) & 0xFF,
        (frf >>  8) & 0xFF,
        (frf      ) & 0xFF,
    };
    return sx1262_write_cmd(dev, SX1262_CMD_SET_RF_FREQUENCY, buf, 4);
}

int8_t sx1262_get_rssi_inst(sx1262_t *dev)
{
    uint8_t raw = 0;
    sx1262_read_cmd(dev, SX1262_CMD_GET_RSSI_INST, &raw, 1);
    return -(int8_t)(raw >> 1);  // RSSI = -RssiInst/2 dBm
}

// -------------------------------------------------------
// Full init sequence
// -------------------------------------------------------

esp_err_t sx1262_init(sx1262_t *dev)
{
    ESP_LOGI(TAG, "Initializing SX1262...");

    // Hardware reset
    gpio_set_level(dev->pin_reset, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(dev->pin_reset, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    sx1262_wait_busy(dev);

    // Set TCXO control via DIO3 (1.8V, 5ms startup)
    uint8_t tcxo[4] = {0x06, 0x00, 0x00, 0x64}; // 1.8V, timeout=100*15.625us=1.5625ms
    sx1262_write_cmd(dev, SX1262_CMD_SET_DIO3_AS_TCXO_CTRL, tcxo, 4);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Calibrate all blocks
    uint8_t calib_param = 0x7F;
    sx1262_write_cmd(dev, SX1262_CMD_CALIBRATE, &calib_param, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    sx1262_wait_busy(dev);

    // Use DC-DC regulator
    uint8_t regmode = SX1262_USE_DCDC ? 0x01 : 0x00;
    sx1262_write_cmd(dev, SX1262_CMD_SET_REGULATOR_MODE, &regmode, 1);

    // Standby XOSC
    sx1262_set_standby(dev, SX1262_STANDBY_XOSC);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Set packet type to LoRa (required before modulation params)
    uint8_t pkt_type = SX1262_PACKET_TYPE_LORA;
    sx1262_write_cmd(dev, SX1262_CMD_SET_PACKET_TYPE, &pkt_type, 1);

    // Fix TX clamp issue
    sx1262_write_register(dev, SX1262_REG_TX_CLAMP_CFG, 0x1E);

    // Image calibration for 868-928 MHz band
    uint8_t img_calib[2] = {0xD7, 0xDB};  // 868-928 MHz
    sx1262_write_cmd(dev, SX1262_CMD_CALIBRATE_IMAGE, img_calib, 2);
    vTaskDelay(pdMS_TO_TICKS(10));
    sx1262_wait_busy(dev);

    ESP_LOGI(TAG, "SX1262 init complete");
    return ESP_OK;
}

// -------------------------------------------------------
// Prepare for RSSI scan (continuous RX at each frequency)
// -------------------------------------------------------

esp_err_t sx1262_prepare_for_scan(sx1262_t *dev)
{
    // Standby first
    sx1262_set_standby(dev, SX1262_STANDBY_XOSC);

    // LoRa modulation: SF7, BW 500kHz, CR 4/5
    // BW 500kHz gives ~500kHz noise floor — good for RSSI sampling
    uint8_t mod_params[4] = {
        0x07,   // SF7
        0x06,   // BW 500 kHz
        0x01,   // CR 4/5
        0x00,   // Low data rate optimize off
    };
    sx1262_write_cmd(dev, SX1262_CMD_SET_MODULATION_PARAMS, mod_params, 4);

    return ESP_OK;
}
