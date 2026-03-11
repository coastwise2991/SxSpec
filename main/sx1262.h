#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// -------------------------------------------------------
// SX1262 Command opcodes
// -------------------------------------------------------
#define SX1262_CMD_SET_STANDBY              0x80
#define SX1262_CMD_SET_RX                   0x82
#define SX1262_CMD_SET_FS                   0xC1
#define SX1262_CMD_SET_RF_FREQUENCY         0x86
#define SX1262_CMD_SET_PACKET_TYPE          0x8A
#define SX1262_CMD_SET_MODULATION_PARAMS    0x8B
#define SX1262_CMD_SET_REGULATOR_MODE       0x96
#define SX1262_CMD_SET_DIO3_AS_TCXO_CTRL   0x97
#define SX1262_CMD_CALIBRATE                0x89
#define SX1262_CMD_CALIBRATE_IMAGE          0x98
#define SX1262_CMD_WRITE_REGISTER           0x0D
#define SX1262_CMD_READ_REGISTER            0x1D
#define SX1262_CMD_GET_RSSI_INST            0x15

// -------------------------------------------------------
// SX1262 Registers
// -------------------------------------------------------
#define SX1262_REG_TX_CLAMP_CFG             0x08D8

// -------------------------------------------------------
// SX1262 Constants
// -------------------------------------------------------
#define SX1262_STANDBY_RC                   0x00
#define SX1262_STANDBY_XOSC                 0x01
#define SX1262_PACKET_TYPE_LORA             0x01

// -------------------------------------------------------
// SX1262 device handle
// -------------------------------------------------------
typedef struct {
    spi_device_handle_t spi;
    gpio_num_t          pin_nss;
    gpio_num_t          pin_reset;
    gpio_num_t          pin_busy;
    gpio_num_t          pin_dio1;
} sx1262_t;

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

/**
 * @brief Wait until the SX1262 BUSY pin goes low.
 */
void sx1262_wait_busy(sx1262_t *dev);

/**
 * @brief Send a command with optional data bytes.
 */
esp_err_t sx1262_write_cmd(sx1262_t *dev, uint8_t cmd, const uint8_t *data, size_t len);

/**
 * @brief Send a command and read back response bytes.
 */
esp_err_t sx1262_read_cmd(sx1262_t *dev, uint8_t cmd, uint8_t *buf, size_t len);

/**
 * @brief Put the radio into standby mode.
 * @param mode SX1262_STANDBY_RC or SX1262_STANDBY_XOSC
 */
esp_err_t sx1262_set_standby(sx1262_t *dev, uint8_t mode);

/**
 * @brief Set the RF carrier frequency.
 * @param freq_hz Frequency in Hz (e.g. 895000000)
 */
esp_err_t sx1262_set_frequency(sx1262_t *dev, uint32_t freq_hz);

/**
 * @brief Read the instantaneous RSSI value.
 * @return RSSI in dBm (negative value)
 */
int8_t sx1262_get_rssi_inst(sx1262_t *dev);

/**
 * @brief Full hardware + software initialization sequence.
 */
esp_err_t sx1262_init(sx1262_t *dev);

/**
 * @brief Configure modulation params ready for RSSI scanning.
 */
esp_err_t sx1262_prepare_for_scan(sx1262_t *dev);
