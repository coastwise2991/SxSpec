#pragma once

#include <stdint.h>

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

/**
 * @brief Convert a normalized RSSI level (0-255) to an RGB565 thermal color.
 *        Palette: black -> blue -> cyan -> green -> yellow -> red -> white
 */
uint16_t rssi_to_color(uint8_t level);

/**
 * @brief Clear the waterfall buffer (all zeros).
 */
void waterfall_init(void);

/**
 * @brief Push a new row of RSSI samples to the top of the waterfall,
 *        scrolling all existing rows down by one.
 * @param row  Array of normalized RSSI values (0-255)
 * @param len  Number of samples (clamped to WATERFALL_WIDTH)
 */
void waterfall_push_row(uint8_t *row, uint16_t len);

/**
 * @brief Render only the top (newest) row to the LCD.
 *        Faster than a full redraw; call every scan cycle.
 */
void waterfall_render_top_row(void);

/**
 * @brief Render the entire waterfall buffer to the LCD.
 *        Call periodically to correct any display drift.
 */
void waterfall_render(void);

/**
 * @brief Draw the header bar showing center frequency and span.
 * @param center_hz Center frequency in Hz
 * @param span_hz   Span in Hz
 */
void waterfall_draw_header(uint32_t center_hz, uint32_t span_hz);
