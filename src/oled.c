#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "font.h"
#include "history.h"
#include "oled.h"

#define OLED_I2C_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)

#define CHART_TOP 8 // rows 0-6 are the text line, row 7 is a gap
#define CHART_HEIGHT (OLED_HEIGHT - CHART_TOP)

static const char *TAG = "OLED";

static uint8_t s_fb[OLED_WIDTH * OLED_PAGES];

static esp_err_t oled_send(uint8_t control_byte, const uint8_t *payload, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, control_byte, true);
    i2c_master_write(cmd, payload, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write to 0x%02X failed: %s", OLED_I2C_ADDR, esp_err_to_name(ret));
    }
    return ret;
}

static void oled_set_addr_window(void) {
    uint8_t cmds[] = {
        0x21, 0x00, OLED_WIDTH - 1,   // column address range
        0x22, 0x00, OLED_PAGES - 1,   // page address range
    };
    oled_send(0x00, cmds, sizeof(cmds));
}

static void oled_flush(void) {
    oled_set_addr_window();
    oled_send(0x40, s_fb, sizeof(s_fb));
}

static void set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    uint8_t mask = 1 << (y % 8);
    int idx = page * OLED_WIDTH + x;
    if (on) {
        s_fb[idx] |= mask;
    } else {
        s_fb[idx] &= ~mask;
    }
}

static int draw_char(int x0, int y0, char ch, int scale) {
    const uint8_t *glyph = font_lookup(ch);
    if (glyph == NULL) glyph = font_lookup(' ');

    for (int col = 0; col < 5; col++) {
        uint8_t colbits = glyph[col];
        for (int row = 0; row < 7; row++) {
            bool on = (colbits >> row) & 1;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    set_pixel(x0 + col * scale + sx, y0 + row * scale + sy, on);
                }
            }
        }
    }
    return (5 + 1) * scale; // glyph width + 1 column of spacing, scaled
}

static void draw_string(int x0, int y0, const char *str, int scale) {
    int x = x0;
    for (const char *p = str; *p != '\0'; p++) {
        x += draw_char(x, y0, *p, scale);
    }
}

static void draw_line(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        set_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_rssi_chart(void) {
    int values[RSSI_HISTORY_CAP];
    int n = rssi_history_get(values, RSSI_HISTORY_CAP);
    if (n < 2) return; // need at least two points to draw a line

    int lo = values[0], hi = values[0];
    for (int i = 1; i < n; i++) {
        if (values[i] < lo) lo = values[i];
        if (values[i] > hi) hi = values[i];
    }
    if (lo == hi) { lo -= 1; hi += 1; } // keep a flat history visible, not a divide-by-zero

    int prev_x = 0, prev_y = 0;
    for (int i = 0; i < n; i++) {
        int x = (i * (OLED_WIDTH - 1)) / (n - 1);
        int y = (OLED_HEIGHT - 1) - ((values[i] - lo) * (CHART_HEIGHT - 1)) / (hi - lo);
        if (i > 0) draw_line(prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

void oled_init(void) {
    static const uint8_t init_cmds[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divide ratio / oscillator frequency
        0xA8, 0x3F,       // multiplex ratio = 64
        0xD3, 0x00,       // display offset = 0
        0x40,             // start line = 0
        0x8D, 0x14,       // charge pump enable
        0x20, 0x00,       // memory addressing mode = horizontal
        0xA1,             // segment remap
        0xC8,             // COM scan direction, remapped
        0xDA, 0x12,       // COM pins hardware config
        0x81, 0xCF,       // contrast
        0xD9, 0xF1,       // pre-charge period
        0xDB, 0x40,       // VCOMH deselect level
        0xA4,             // resume to RAM content display
        0xA6,             // normal (not inverted) display
        0xAF,             // display on
    };
    esp_err_t err = oled_send(0x00, init_cmds, sizeof(init_cmds));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Init sequence ACKed by 0x%02X", OLED_I2C_ADDR);
    }

    memset(s_fb, 0, sizeof(s_fb));
    oled_flush();
}

void oled_render(int rssi, const char *mode) {
    memset(s_fb, 0, sizeof(s_fb));

    char text[16];
    snprintf(text, sizeof(text), "RSSI:%d", rssi);
    draw_string(0, 0, text, 1);

    int mode_width = (int)strlen(mode) * (5 + 1);
    draw_string(OLED_WIDTH - mode_width, 0, mode, 1);

    rssi_history_push(rssi);
    draw_rssi_chart();

    oled_flush();
}

void oled_show_bitmap(const uint8_t *bitmap) {
    memset(s_fb, 0, sizeof(s_fb));

    const int bytes_per_row = OLED_WIDTH / 8; // 16
    for (int y = 0; y < OLED_HEIGHT; y++) {
        for (int x = 0; x < OLED_WIDTH; x++) {
            int byte_idx = y * bytes_per_row + (x / 8);
            int bit_idx = 7 - (x % 8); // MSB first
            bool on = (bitmap[byte_idx] >> bit_idx) & 1;
            set_pixel(x, y, on);
        }
    }

    oled_flush();
}
