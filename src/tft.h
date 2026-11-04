#ifndef TFT_H
#define TFT_H

#include <stdint.h>

// Initializes the ILI9341 TFT over SPI (240x320 portrait).
// Call spi_bus_init() first.
void tft_init(void);

// Draws one frame: RSSI at top-left, mode label at top-right, and a
// rolling line chart of recent RSSI values filling the rest of the
// screen. Call once per sample - it tracks its own RSSI history
// (shared with the OLED driver's history buffer).
void tft_render(int rssi, const char *mode);

// Shows a "WELCOME" splash screen (teal background, white rounded
// label box, dark text). Call once at boot before the sampling loop.
void tft_show_welcome(void);

// Shows the mode-select screen (title bar + WIFI/BLE/LORA boxes).
void tft_show_mode_select(void);

// Given a touch point in screen coordinates, returns which mode box it
// falls in: 0=WIFI, 1=BLE, 2=LORA, or -1 if outside all three.
int tft_mode_select_hit_test(int x, int y);

// Shows the WiFi-select screen: SSID/sample-count header, a Detail
// button, and RSSI/AVG/MAX/MIN stat boxes (values shown as "...."
// placeholders - not yet wired to live data).
void tft_show_wifi_select(const char *ssid, int sample_count);

// Redraws just the RSSI/AVG/MAX/MIN stat boxes on the WiFi-select
// screen with live values. Call once per sample after
// tft_show_wifi_select() has drawn the rest of the screen.
void tft_update_wifi_stats(int rssi, int avg_rssi, int max_rssi, int min_rssi);

// Clears the screen and shows a single centered status message
// (e.g. "WIFI NOT CONNECTED"). Only characters in font.c render;
// anything else shows as a blank space.
void tft_show_message(const char *msg);

// Draws a full RGB565 image (row-major, one uint16_t per pixel) at the
// top-left of the screen, clipped to the screen's own dimensions.
void tft_draw_rgb565_image(const uint16_t *pixels, int width, int height);

#endif
