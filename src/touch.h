#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>

// Adds the XPT2046 as a device on the already-initialized SPI bus
// (call spi_bus_init() first).
void touch_init(void);

// Returns true if the panel is currently being touched, and writes the
// raw 12-bit ADC readings (0-4095) to *x_raw/*y_raw. These are NOT
// screen pixel coordinates yet - just the uncalibrated ADC values.
bool touch_read_raw(int *x_raw, int *y_raw);

// Same as touch_read_raw(), but maps the result to screen pixel
// coordinates (0..TFT_WIDTH-1, 0..TFT_HEIGHT-1) using approximate
// default calibration constants - verify against real taps and adjust
// TOUCH_RAW_*_MIN/MAX in touch.c if points land in the wrong place.
bool touch_read_screen_xy(int *sx, int *sy);

// Raw PENIRQ pin state: true if the panel reports it's being touched
// right now (regardless of whether an SPI read has happened). Useful
// for diagnosing whether the IRQ line itself is wired/working.
bool touch_irq_active(void);

#endif
