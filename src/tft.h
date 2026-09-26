#ifndef TFT_H
#define TFT_H

// Initializes the ILI9341 TFT over SPI (320x240 landscape).
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

// Clears the screen and shows a single centered status message
// (e.g. "WIFI NOT CONNECTED"). Only characters in font.c render;
// anything else shows as a blank space.
void tft_show_message(const char *msg);

#endif
