#ifndef TFT_H
#define TFT_H

#include <stdint.h>
#include <stdbool.h>

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

// Shows the WiFi-select screen: SSID header (mode runs continuously
// until the back button is tapped), a Detail button, and RSSI/AVG/
// MAX/MIN stat boxes (values shown as "...." placeholders - not yet
// wired to live data).
void tft_show_wifi_select(const char *ssid);

// Redraws the RSSI/LATENCY/JITTER main-box lines and the AVG/MAX/MIN
// RSSI stat boxes on the WiFi-select screen with live values. Pass
// jitter_ms < 0 before the second successful ping (not enough samples
// yet) to show a placeholder instead. Call once per sample after
// tft_show_wifi_select() has drawn the rest of the screen.
void tft_update_wifi_stats(int rssi, int rtt_ms, int jitter_ms, bool lost, int avg_rssi, int max_rssi, int min_rssi);

// Shows the LoRa-receiver screen: frequency header, and RSSI/SNR/packet
// count/AVG/MAX/MIN RSSI boxes (shown as placeholders initially).
void tft_show_lora_screen(long frequency_hz);

// Redraws the LoRa stat boxes with live values. Call once per received
// packet after tft_show_lora_screen() has drawn the rest of the screen.
void tft_update_lora_stats(int rssi, int snr, int packet_count, int avg_rssi, int max_rssi, int min_rssi);

// Shows the BLE-scanner screen: target-name header, and STATUS/RSSI/
// AVG/MAX/MIN RSSI boxes (shown as placeholders initially).
void tft_show_ble_screen(const char *target_name);

// Redraws the BLE stat boxes with live values. Call once per scan tick
// after tft_show_ble_screen() has drawn the rest of the screen.
void tft_update_ble_stats(bool found, int rssi, int avg_rssi, int max_rssi, int min_rssi);

// Shows `title`, then up to TFT_LIST_MAX_ROWS tappable rows, one per
// string in `lines`. Pass count=0 to show `empty_text` instead (e.g.
// "SCANNING...." while a BLE scan hasn't found anything yet).
void tft_show_selection_list(const char *title, const char *const *lines, int count, const char *empty_text);

// Given a touch point, returns which row index it falls in (0..count-1),
// or -1 if outside all rows.
int tft_selection_list_hit_test(int x, int y, int count);

// Clears the screen and shows a single centered status message
// (e.g. "WIFI NOT CONNECTED"). Only characters in font.c render;
// anything else shows as a blank space.
void tft_show_message(const char *msg);

// Draws a small "MENU" button, shown on every mode screen so the user
// can return to mode-select. Caller is responsible for checking taps
// against it via tft_back_button_hit_test().
void tft_draw_back_button(void);

// True if the given touch point falls on the back button.
bool tft_back_button_hit_test(int x, int y);

#endif
