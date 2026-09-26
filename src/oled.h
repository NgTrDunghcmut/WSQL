#ifndef OLED_H
#define OLED_H

// Initializes the SSD1306/SSD1315 OLED over I2C.
// Call i2c_bus_init() first.
void oled_init(void);

// Draws one frame: RSSI at top-left, mode label at top-right, and a
// rolling line chart of recent RSSI values filling the rest of the
// screen. Call once per sample - it tracks its own RSSI history.
void oled_render(int rssi, const char *mode);

// Draws a full-screen 128x64 1bpp bitmap and flushes it to the display.
// Format: row-major, MSB-first per byte (16 bytes/row x 64 rows = 1024
// bytes) - the layout tools like image2cpp export by default.
void oled_show_bitmap(const uint8_t *bitmap);

#endif
