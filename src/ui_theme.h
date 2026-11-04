#ifndef UI_THEME_H
#define UI_THEME_H

// Central place for every screen dimension, color, and layout/scale
// constant used by the display drivers (oled.c, tft.c). Change a value
// here and every screen that uses it picks it up.

// ---- OLED (SSD1306/SSD1315, 128x64, monochrome) ----
#define OLED_WIDTH  128
#define OLED_HEIGHT 64

#define OLED_CHART_TOP  8  // rows 0-6 are the text line, row 7 is a gap
#define OLED_TEXT_SCALE 1  // oled_render() RSSI/mode label scale

// ---- TFT (ILI9341, 240x320 portrait, RGB565 color) ----
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Colors (RGB565)
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

// Shared app palette (from the DisplayKit design) - teal background,
// white label/title box, dark navy text.
#define TFT_BG           0x55D4
#define TFT_WELCOME_BOX  0xFFFF
#define TFT_WELCOME_TEXT 0x10C4

// Mode-select screen
#define TFT_MODE_BOX_COLOR 0xE6EA // WiFi/BLE/LoRa selectable box fill
#define TFT_TITLE_BOX      0xFFFF // title bar background
#define TFT_TITLE_TEXT     0x10C4

// WiFi-select screen
#define TFT_WIFI_DATA_BOX 0xDF0F // outer RSSI data box
#define TFT_WIFI_STAT_BOX 0xB596 // AVG/MAX/MIN stat sub-boxes

// Layout
#define TFT_CHART_TOP 28  // below the 7x3=21px text row plus a gap

// Text scale factors
#define TFT_RENDER_TEXT_SCALE  3  // tft_render() RSSI/mode label scale
#define TFT_MESSAGE_TEXT_SCALE 2  // tft_show_message() scale
#define TFT_WELCOME_TEXT_SCALE 3  // tft_show_welcome() "WELCOME" scale
#define TFT_TITLE_TEXT_SCALE   1  // mode-select title bar text scale
#define TFT_MODE_TEXT_SCALE    2  // mode-select box label scale

// Welcome screen label box
#define TFT_WELCOME_BOX_W 200
#define TFT_WELCOME_BOX_H 50
#define TFT_WELCOME_BOX_R 10

// Mode-select screen layout (matches the DisplayKit reference, 240x320)
#define TFT_MODE_BOX_X 21
#define TFT_MODE_BOX_W 198
#define TFT_MODE_BOX_H 39
#define TFT_MODE_BOX_WIFI_Y  24
#define TFT_MODE_BOX_BLE_Y   79
#define TFT_MODE_BOX_LORA_Y 135

#define TFT_MODE_LABEL_OFFSET_X 13 // label position relative to its box's top-left
#define TFT_MODE_LABEL_OFFSET_Y 10

#define TFT_TITLE_BOX_X 25
#define TFT_TITLE_BOX_Y 8
#define TFT_TITLE_BOX_W 200
#define TFT_TITLE_BOX_H 10
#define TFT_TITLE_UNDERLINE_Y 17
#define TFT_TITLE_TEXT_X 29
#define TFT_TITLE_TEXT_Y 9

// Stat-screen layout (matches the DisplayKit reference, 240x320) - shared
// by both the WiFi-select and LoRa-receiver screens, since they're the
// same "outer box + header + 1 big box + 3 small boxes" shape.
#define TFT_WIFI_TEXT_SCALE 1
#define TFT_LORA_LINE_SPACING 12 // vertical gap between lines inside the big box

#define TFT_WIFI_OUTER_X 10
#define TFT_WIFI_OUTER_Y 48
#define TFT_WIFI_OUTER_W 220
#define TFT_WIFI_OUTER_H 120

#define TFT_WIFI_HEADER_X 8
#define TFT_WIFI_HEADER_Y 8
#define TFT_WIFI_HEADER_W 200
#define TFT_WIFI_HEADER_H 35
#define TFT_WIFI_HEADER_LINE1_X 12
#define TFT_WIFI_HEADER_LINE1_Y 12
#define TFT_WIFI_HEADER_LINE2_X 12
#define TFT_WIFI_HEADER_LINE2_Y 24

#define TFT_WIFI_DETAIL_BTN_X 20
#define TFT_WIFI_DETAIL_BTN_Y 52
#define TFT_WIFI_DETAIL_BTN_W 80
#define TFT_WIFI_DETAIL_BTN_H 20
#define TFT_WIFI_DETAIL_BTN_R 4

#define TFT_WIFI_RSSI_BOX_X 20
#define TFT_WIFI_RSSI_BOX_Y 80
#define TFT_WIFI_RSSI_BOX_W 204
#define TFT_WIFI_RSSI_BOX_H 80
#define TFT_WIFI_RSSI_BOX_R 4
#define TFT_WIFI_RSSI_TEXT_OFF_X 8
#define TFT_WIFI_RSSI_TEXT_OFF_Y 8

#define TFT_WIFI_AVG_BOX_Y 180
#define TFT_WIFI_MAX_BOX_Y 220
#define TFT_WIFI_MIN_BOX_Y 260
#define TFT_WIFI_STAT_BOX_H 28
#define TFT_WIFI_STAT_TEXT_OFF_X 8
#define TFT_WIFI_STAT_TEXT_OFF_Y 4

// "Back to menu" button - shown on every mode screen (WiFi, BLE, LoRa)
#define TFT_BACK_BTN_X 70
#define TFT_BACK_BTN_Y 292
#define TFT_BACK_BTN_W 100
#define TFT_BACK_BTN_H 24
#define TFT_BACK_BTN_R 4
#define TFT_BACK_BTN_COLOR TFT_WELCOME_BOX
#define TFT_BACK_BTN_TEXT  TFT_TITLE_TEXT
#define TFT_BACK_BTN_TEXT_SCALE 1

#endif
