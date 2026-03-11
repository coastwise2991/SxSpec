#include "waterfall.h"
#include "st7789.h"
#include "board_config.h"
#include <string.h>
#include <stdio.h>

// -------------------------------------------------------
// Thermal color palette: black -> blue -> cyan -> green -> yellow -> red -> white
// -------------------------------------------------------

uint16_t rssi_to_color(uint8_t level)
{
    // 6-segment gradient
    uint8_t r, g, b;

    if (level < 43) {
        // Black to Blue
        r = 0;
        g = 0;
        b = level * 6;
    } else if (level < 85) {
        // Blue to Cyan
        uint8_t t = level - 43;
        r = 0;
        g = t * 6;
        b = 255;
    } else if (level < 128) {
        // Cyan to Green
        uint8_t t = level - 85;
        r = 0;
        g = 255;
        b = 255 - t * 6;
    } else if (level < 171) {
        // Green to Yellow
        uint8_t t = level - 128;
        r = t * 6;
        g = 255;
        b = 0;
    } else if (level < 213) {
        // Yellow to Red
        uint8_t t = level - 171;
        r = 255;
        g = 255 - t * 6;
        b = 0;
    } else {
        // Red to White
        uint8_t t = level - 213;
        r = 255;
        g = t * 6;
        b = t * 6;
    }

    return RGB565(r, g, b);
}

// -------------------------------------------------------
// Waterfall buffer: [row][col] RSSI levels 0-255
// Row 0 = newest (top of waterfall area)
// -------------------------------------------------------

static uint8_t wf_buf[WATERFALL_HEIGHT][WATERFALL_WIDTH];

void waterfall_init(void)
{
    memset(wf_buf, 0, sizeof(wf_buf));
}

void waterfall_push_row(uint8_t *row, uint16_t len)
{
    // Scroll all rows down by 1 (memmove row by row from bottom)
    memmove(&wf_buf[1], &wf_buf[0],
            (WATERFALL_HEIGHT - 1) * WATERFALL_WIDTH);

    // Copy new row to top (clamp width)
    uint16_t copy_len = len < WATERFALL_WIDTH ? len : WATERFALL_WIDTH;
    memcpy(wf_buf[0], row, copy_len);
    if (copy_len < WATERFALL_WIDTH) {
        memset(&wf_buf[0][copy_len], 0, WATERFALL_WIDTH - copy_len);
    }
}

void waterfall_render_top_row(void)
{
    static uint16_t row_pixels[WATERFALL_WIDTH];
    for (int x = 0; x < WATERFALL_WIDTH; x++) {
        row_pixels[x] = rssi_to_color(wf_buf[0][x]);
    }
    st7789_draw_hline(0, HEADER_HEIGHT, WATERFALL_WIDTH, row_pixels);
}

void waterfall_render(void)
{
    static uint16_t row_pixels[WATERFALL_WIDTH];
    for (int row = 0; row < WATERFALL_HEIGHT; row++) {
        for (int x = 0; x < WATERFALL_WIDTH; x++) {
            row_pixels[x] = rssi_to_color(wf_buf[row][x]);
        }
        st7789_draw_hline(0, HEADER_HEIGHT + row, WATERFALL_WIDTH, row_pixels);
    }
}

void waterfall_draw_header(uint32_t center_hz, uint32_t span_hz)
{
    // Clear header bar
    st7789_fill_rect(0, 0, LCD_WIDTH, HEADER_HEIGHT, COLOR_BLACK);

    // Format: "895.0MHz ±2.5MHz"
    uint32_t center_mhz_int  = center_hz / 1000000;
    uint32_t center_mhz_frac = (center_hz % 1000000) / 100000;
    uint32_t span_khz         = span_hz / 1000;

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu.%luMHz  %luKHz span",
             center_mhz_int, center_mhz_frac, span_khz);

    st7789_write_string(2, 4, buf, COLOR_CYAN, COLOR_BLACK);

    // Draw a thin separator line
    st7789_fill_rect(0, HEADER_HEIGHT - 1, LCD_WIDTH, 1, RGB565(40,40,40));
}
