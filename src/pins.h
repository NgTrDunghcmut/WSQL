#ifndef PINS_H
#define PINS_H

// Central place for every GPIO assignment in this project.
// Change a pin here and every module that uses it picks it up.

// ---- I2C bus (OLED display) - currently disabled in main.c ----
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ---- SPI bus (TFT display) ----
#define SPI_SCK_PIN  5
#define SPI_MOSI_PIN 18
#define SPI_MISO_PIN 22

// ---- TFT control lines (CS is passed to the SPI driver, not toggled manually) ----
#define TFT_CS_PIN  23                                                                                                          
#define TFT_DC_PIN  19
#define TFT_RST_PIN 21

// ---- XPT2046 touch controller (shares SCK/MOSI/MISO with the TFT's SPI bus) ----
#define TOUCH_CS_PIN  32
#define TOUCH_IRQ_PIN 35

#endif
