#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "font.h"
#include "history.h"
#include "tft.h"
#include "pins.h"
#include "ui_theme.h"

#define CHART_HEIGHT (TFT_HEIGHT - TFT_CHART_TOP)

// ILI9341 commands
#define ILI9341_SWRESET 0x01
#define ILI9341_SLPOUT  0x11
#define ILI9341_INVOFF  0x20
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_PASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_MADCTL  0x36
#define ILI9341_COLMOD  0x3A

static const char *TAG = "TFT";
static spi_device_handle_t s_spi;

static void tft_gpio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TFT_DC_PIN) | (1ULL << TFT_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
}

static void tft_reset(void) {
    gpio_set_level(TFT_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

static void tft_send(uint8_t dc_level, const uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(TFT_DC_PIN, dc_level);

    spi_transaction_t t = {0};
    t.length = len * 8;
    t.tx_buffer = data;

    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
    }
}

static void tft_write_cmd(uint8_t cmd) {
    tft_send(0, &cmd, 1); // DC=0 -> command
}

static void tft_write_data(const uint8_t *data, size_t len) {
    tft_send(1, data, len); // DC=1 -> data
}

static void tft_write_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    tft_write_cmd(cmd);
    tft_write_data(data, len);
}

static void tft_set_addr_window(int x0, int y0, int x1, int y1) {
    uint8_t col[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    tft_write_cmd_data(ILI9341_CASET, col, sizeof(col));
    tft_write_cmd_data(ILI9341_PASET, row, sizeof(row));
    tft_write_cmd(ILI9341_RAMWR);
}

static void tft_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT || w <= 0 || h <= 0) return;
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;

    tft_set_addr_window(x, y, x + w - 1, y + h - 1);

    uint8_t row_buf[TFT_WIDTH * 2];
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (int i = 0; i < w; i++) {
        row_buf[i * 2] = hi;
        row_buf[i * 2 + 1] = lo;
    }
    for (int r = 0; r < h; r++) {
        tft_write_data(row_buf, (size_t)w * 2);
    }
}

static void tft_fill_circle(int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        tft_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

static void tft_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;

    tft_fill_rect(x + r, y, w - 2 * r, h, color);         // center body
    tft_fill_rect(x, y + r, r, h - 2 * r, color);          // left edge
    tft_fill_rect(x + w - r, y + r, r, h - 2 * r, color);  // right edge

    tft_fill_circle(x + r, y + r, r, color);               // top-left
    tft_fill_circle(x + w - r - 1, y + r, r, color);       // top-right
    tft_fill_circle(x + r, y + h - r - 1, r, color);       // bottom-left
    tft_fill_circle(x + w - r - 1, y + h - r - 1, r, color); // bottom-right
}

static void tft_draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) return;
    uint8_t data[2] = { color >> 8, color & 0xFF };
    tft_set_addr_window(x, y, x, y);
    tft_write_data(data, sizeof(data));
}

static int draw_char(int x0, int y0, char ch, int scale, uint16_t color) {
    const uint8_t *glyph = font_lookup(ch);
    if (glyph == NULL) glyph = font_lookup(' ');

    for (int col = 0; col < 5; col++) {
        uint8_t colbits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if ((colbits >> row) & 1) {
                tft_fill_rect(x0 + col * scale, y0 + row * scale, scale, scale, color);
            }
        }
    }
    return (5 + 1) * scale;
}

static void draw_string(int x0, int y0, const char *str, int scale, uint16_t color) {
    int x = x0;
    for (const char *p = str; *p != '\0'; p++) {
        x += draw_char(x, y0, *p, scale, color);
    }
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        tft_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_rssi_chart(void) {
    int values[RSSI_HISTORY_CAP];
    int n = rssi_history_get(values, RSSI_HISTORY_CAP);
    if (n < 2) return;

    int lo = values[0], hi = values[0];
    for (int i = 1; i < n; i++) {
        if (values[i] < lo) lo = values[i];
        if (values[i] > hi) hi = values[i];
    }
    if (lo == hi) { lo -= 1; hi += 1; }

    int prev_x = 0, prev_y = 0;
    for (int i = 0; i < n; i++) {
        int x = (i * (TFT_WIDTH - 1)) / (n - 1);
        int y = (TFT_HEIGHT - 1) - ((values[i] - lo) * (CHART_HEIGHT - 1)) / (hi - lo);
        if (i > 0) draw_line(prev_x, prev_y, x, y, TFT_WHITE);
        prev_x = x;
        prev_y = y;
    }
}

void tft_init(void) {
    tft_gpio_init();
    tft_reset();

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_10M, // lower than 20M - more tolerant of breadboard jumper wiring
        .mode = 0,
        .spics_io_num = TFT_CS_PIN,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(SPI3_HOST, &devcfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return;
    }

    tft_write_cmd(ILI9341_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_write_cmd(ILI9341_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t colmod = 0x55; // 16 bits/pixel (RGB565)
    tft_write_cmd_data(ILI9341_COLMOD, &colmod, 1);

    uint8_t madctl = 0x48; // portrait (MX) + BGR color order
    tft_write_cmd_data(ILI9341_MADCTL, &madctl, 1);

    tft_write_cmd(ILI9341_INVOFF);
    tft_write_cmd(ILI9341_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "ILI9341 init sequence sent");

    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BLACK);
}

void tft_render(int rssi, const char *mode) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_CHART_TOP, TFT_BLACK); // clear the text row only

    char text[16];
    snprintf(text, sizeof(text), "RSSI:%d", rssi);
    draw_string(0, 0, text, TFT_RENDER_TEXT_SCALE, TFT_WHITE);

    int mode_width = (int)strlen(mode) * (5 + 1) * TFT_RENDER_TEXT_SCALE;
    draw_string(TFT_WIDTH - mode_width, 0, mode, TFT_RENDER_TEXT_SCALE, TFT_WHITE);

    tft_fill_rect(0, TFT_CHART_TOP, TFT_WIDTH, CHART_HEIGHT, TFT_BLACK); // clear the chart area
    rssi_history_push(rssi);
    draw_rssi_chart();
}

void tft_show_welcome(void) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BG);

    int box_x = (TFT_WIDTH - TFT_WELCOME_BOX_W) / 2;
    int box_y = (TFT_HEIGHT - TFT_WELCOME_BOX_H) / 2;
    tft_fill_round_rect(box_x, box_y, TFT_WELCOME_BOX_W, TFT_WELCOME_BOX_H, TFT_WELCOME_BOX_R, TFT_WELCOME_BOX);

    const char *msg = "WELCOME";
    int text_width = (int)strlen(msg) * (5 + 1) * TFT_WELCOME_TEXT_SCALE;
    int text_x = box_x + (TFT_WELCOME_BOX_W - text_width) / 2;
    int text_y = box_y + (TFT_WELCOME_BOX_H - 7 * TFT_WELCOME_TEXT_SCALE) / 2;
    draw_string(text_x, text_y, msg, TFT_WELCOME_TEXT_SCALE, TFT_WELCOME_TEXT);
}

void tft_show_mode_select(void) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BG);

    // Title bar: white background strip + underline, dark navy text.
    tft_fill_rect(TFT_TITLE_BOX_X, TFT_TITLE_BOX_Y, TFT_TITLE_BOX_W, TFT_TITLE_BOX_H, TFT_TITLE_BOX);
    draw_line(TFT_TITLE_BOX_X, TFT_TITLE_UNDERLINE_Y, TFT_TITLE_BOX_X + TFT_TITLE_BOX_W, TFT_TITLE_UNDERLINE_Y, TFT_TITLE_TEXT);
    draw_string(TFT_TITLE_TEXT_X, TFT_TITLE_TEXT_Y, "WIRELESS SIGNAL QUALITY LOGGER", TFT_TITLE_TEXT_SCALE, TFT_TITLE_TEXT);

    // Three selectable mode boxes.
    tft_fill_rect(TFT_MODE_BOX_X, TFT_MODE_BOX_WIFI_Y, TFT_MODE_BOX_W, TFT_MODE_BOX_H, TFT_MODE_BOX_COLOR);
    tft_fill_rect(TFT_MODE_BOX_X, TFT_MODE_BOX_BLE_Y, TFT_MODE_BOX_W, TFT_MODE_BOX_H, TFT_MODE_BOX_COLOR);
    tft_fill_rect(TFT_MODE_BOX_X, TFT_MODE_BOX_LORA_Y, TFT_MODE_BOX_W, TFT_MODE_BOX_H, TFT_MODE_BOX_COLOR);

    draw_string(TFT_MODE_BOX_X + TFT_MODE_LABEL_OFFSET_X, TFT_MODE_BOX_WIFI_Y + TFT_MODE_LABEL_OFFSET_Y,
                "WIFI", TFT_MODE_TEXT_SCALE, TFT_TITLE_TEXT);
    draw_string(TFT_MODE_BOX_X + TFT_MODE_LABEL_OFFSET_X, TFT_MODE_BOX_BLE_Y + TFT_MODE_LABEL_OFFSET_Y,
                "BLE", TFT_MODE_TEXT_SCALE, TFT_TITLE_TEXT);
    draw_string(TFT_MODE_BOX_X + TFT_MODE_LABEL_OFFSET_X, TFT_MODE_BOX_LORA_Y + TFT_MODE_LABEL_OFFSET_Y,
                "LORA", TFT_MODE_TEXT_SCALE, TFT_TITLE_TEXT);
}

int tft_mode_select_hit_test(int x, int y) {
    if (x < TFT_MODE_BOX_X || x >= TFT_MODE_BOX_X + TFT_MODE_BOX_W) return -1;

    if (y >= TFT_MODE_BOX_WIFI_Y && y < TFT_MODE_BOX_WIFI_Y + TFT_MODE_BOX_H) return 0;
    if (y >= TFT_MODE_BOX_BLE_Y && y < TFT_MODE_BOX_BLE_Y + TFT_MODE_BOX_H) return 1;
    if (y >= TFT_MODE_BOX_LORA_Y && y < TFT_MODE_BOX_LORA_Y + TFT_MODE_BOX_H) return 2;
    return -1;
}

void tft_show_wifi_select(const char *ssid, int sample_count) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BG);

    tft_fill_rect(TFT_WIFI_OUTER_X, TFT_WIFI_OUTER_Y, TFT_WIFI_OUTER_W, TFT_WIFI_OUTER_H, TFT_WIFI_DATA_BOX);

    tft_fill_rect(TFT_WIFI_HEADER_X, TFT_WIFI_HEADER_Y, TFT_WIFI_HEADER_W, TFT_WIFI_HEADER_H, TFT_TITLE_BOX);
    char line[32];
    snprintf(line, sizeof(line), "SAMPLES:%d (DEFAULT)", sample_count);
    draw_string(TFT_WIFI_HEADER_LINE1_X, TFT_WIFI_HEADER_LINE1_Y, line, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    snprintf(line, sizeof(line), "SSID:%s", ssid);
    draw_string(TFT_WIFI_HEADER_LINE2_X, TFT_WIFI_HEADER_LINE2_Y, line, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_DETAIL_BTN_X, TFT_WIFI_DETAIL_BTN_Y, TFT_WIFI_DETAIL_BTN_W, TFT_WIFI_DETAIL_BTN_H,
                         TFT_WIFI_DETAIL_BTN_R, TFT_WELCOME_BOX);
    draw_string(TFT_WIFI_DETAIL_BTN_X + 4, TFT_WIFI_DETAIL_BTN_Y + 4, "DETAIL:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_RSSI_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_RSSI_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y,
                "RSSI: ....", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_AVG_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_AVG_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "AVG RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MAX_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MAX_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "MAX RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MIN_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MIN_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "MIN RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
}

void tft_update_wifi_stats(int rssi, int avg_rssi, int max_rssi, int min_rssi) {
    char buf[32];

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_RSSI_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_RSSI_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "RSSI: %d", rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_AVG_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "AVG RSSI: %d", avg_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_AVG_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MAX_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "MAX RSSI: %d", max_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MAX_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MIN_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "MIN RSSI: %d", min_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MIN_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
}

void tft_show_lora_screen(long frequency_hz) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BG);

    tft_fill_rect(TFT_WIFI_OUTER_X, TFT_WIFI_OUTER_Y, TFT_WIFI_OUTER_W, TFT_WIFI_OUTER_H, TFT_WIFI_DATA_BOX);

    tft_fill_rect(TFT_WIFI_HEADER_X, TFT_WIFI_HEADER_Y, TFT_WIFI_HEADER_W, TFT_WIFI_HEADER_H, TFT_TITLE_BOX);
    draw_string(TFT_WIFI_HEADER_LINE1_X, TFT_WIFI_HEADER_LINE1_Y, "LORA RECEIVER", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    char line[32];
    snprintf(line, sizeof(line), "FREQ:%ldMHZ", frequency_hz / 1000000);
    draw_string(TFT_WIFI_HEADER_LINE2_X, TFT_WIFI_HEADER_LINE2_Y, line, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_RSSI_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_RSSI_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y,
                "RSSI: ....", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y + TFT_LORA_LINE_SPACING,
                "SNR: ....", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y + 2 * TFT_LORA_LINE_SPACING,
                "PACKETS: 0", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_AVG_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_AVG_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "AVG RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MAX_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MAX_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "MAX RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MIN_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MIN_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "MIN RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
}

void tft_update_lora_stats(int rssi, float snr, int packet_count, int avg_rssi, int max_rssi, int min_rssi) {
    char buf[32];

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_RSSI_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_RSSI_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "RSSI: %d", rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    // Format SNR as signed tenths manually - avoid relying on printf's
    // %f, which ESP-IDF's "nano" newlib formatting may not support.
    int snr_tenths = (int)(snr * 10.0f);
    bool snr_negative = snr_tenths < 0;
    if (snr_negative) snr_tenths = -snr_tenths;
    snprintf(buf, sizeof(buf), "SNR: %s%d.%d", snr_negative ? "-" : "", snr_tenths / 10, snr_tenths % 10);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y + TFT_LORA_LINE_SPACING,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    snprintf(buf, sizeof(buf), "PACKETS: %d", packet_count);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y + 2 * TFT_LORA_LINE_SPACING,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_AVG_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "AVG RSSI: %d", avg_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_AVG_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MAX_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "MAX RSSI: %d", max_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MAX_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MIN_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "MIN RSSI: %d", min_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MIN_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
}

void tft_show_ble_screen(const char *target_name) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BG);

    tft_fill_rect(TFT_WIFI_OUTER_X, TFT_WIFI_OUTER_Y, TFT_WIFI_OUTER_W, TFT_WIFI_OUTER_H, TFT_WIFI_DATA_BOX);

    tft_fill_rect(TFT_WIFI_HEADER_X, TFT_WIFI_HEADER_Y, TFT_WIFI_HEADER_W, TFT_WIFI_HEADER_H, TFT_TITLE_BOX);
    draw_string(TFT_WIFI_HEADER_LINE1_X, TFT_WIFI_HEADER_LINE1_Y, "BLE SCANNER", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    char line[32];
    snprintf(line, sizeof(line), "TARGET:%s", target_name);
    draw_string(TFT_WIFI_HEADER_LINE2_X, TFT_WIFI_HEADER_LINE2_Y, line, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_RSSI_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_RSSI_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y,
                "STATUS: ....", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y + TFT_LORA_LINE_SPACING,
                "RSSI: ....", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_AVG_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_AVG_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "AVG RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MAX_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MAX_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "MAX RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MIN_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MIN_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                "MIN RSSI:", TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
}

void tft_update_ble_stats(bool found, int rssi, int avg_rssi, int max_rssi, int min_rssi) {
    char buf[32];

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_RSSI_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_RSSI_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "STATUS: %s", found ? "FOUND" : "LOST");
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
    snprintf(buf, sizeof(buf), "RSSI: %d", rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_RSSI_TEXT_OFF_X, TFT_WIFI_RSSI_BOX_Y + TFT_WIFI_RSSI_TEXT_OFF_Y + TFT_LORA_LINE_SPACING,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_AVG_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "AVG RSSI: %d", avg_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_AVG_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MAX_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "MAX RSSI: %d", max_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MAX_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);

    tft_fill_round_rect(TFT_WIFI_RSSI_BOX_X, TFT_WIFI_MIN_BOX_Y, TFT_WIFI_RSSI_BOX_W, TFT_WIFI_STAT_BOX_H,
                         TFT_WIFI_RSSI_BOX_R, TFT_WIFI_STAT_BOX);
    snprintf(buf, sizeof(buf), "MIN RSSI: %d", min_rssi);
    draw_string(TFT_WIFI_RSSI_BOX_X + TFT_WIFI_STAT_TEXT_OFF_X, TFT_WIFI_MIN_BOX_Y + TFT_WIFI_STAT_TEXT_OFF_Y,
                buf, TFT_WIFI_TEXT_SCALE, TFT_TITLE_TEXT);
}

void tft_show_message(const char *msg) {
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BLACK);

    int char_width = (5 + 1) * TFT_MESSAGE_TEXT_SCALE;
    int text_width = (int)strlen(msg) * char_width;
    int x = (TFT_WIDTH - text_width) / 2;
    int y = (TFT_HEIGHT - 7 * TFT_MESSAGE_TEXT_SCALE) / 2;

    draw_string(x, y, msg, TFT_MESSAGE_TEXT_SCALE, TFT_WHITE);
}

void tft_draw_back_button(void) {
    tft_fill_round_rect(TFT_BACK_BTN_X, TFT_BACK_BTN_Y, TFT_BACK_BTN_W, TFT_BACK_BTN_H,
                         TFT_BACK_BTN_R, TFT_BACK_BTN_COLOR);

    const char *label = "MENU";
    int text_width = (int)strlen(label) * (5 + 1) * TFT_BACK_BTN_TEXT_SCALE;
    int x = TFT_BACK_BTN_X + (TFT_BACK_BTN_W - text_width) / 2;
    int y = TFT_BACK_BTN_Y + (TFT_BACK_BTN_H - 7 * TFT_BACK_BTN_TEXT_SCALE) / 2;
    draw_string(x, y, label, TFT_BACK_BTN_TEXT_SCALE, TFT_BACK_BTN_TEXT);
}

bool tft_back_button_hit_test(int x, int y) {
    return x >= TFT_BACK_BTN_X && x < TFT_BACK_BTN_X + TFT_BACK_BTN_W &&
           y >= TFT_BACK_BTN_Y && y < TFT_BACK_BTN_Y + TFT_BACK_BTN_H;
}
