#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "pins_stats.h"
#include "touch.h"
#include "ui_theme.h"

// XPT2046 control bytes (S=1, channel select, 12-bit, differential mode,
// power-down between conversions so PENIRQ stays enabled).
#define XPT2046_CMD_READ_X 0xD0
#define XPT2046_CMD_READ_Y 0x90

// Approximate default calibration for common 2.8" resistive touch
// panels - raw ADC range mapped to screen pixels. Verify against real
// taps (e.g. corners of the screen) and adjust if points land wrong.
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3900
#define TOUCH_RAW_Y_MIN 200
#define TOUCH_RAW_Y_MAX 3900

static const char *TAG = "Touch";
static spi_device_handle_t s_touch_spi;

void touch_init(void) {
    gpio_config_t irq_conf = {
        .pin_bit_mask = (1ULL << TOUCH_IRQ_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // PENIRQ is open-drain, active low
    };
    gpio_config(&irq_conf);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000, // 1 MHz - well within the XPT2046's spec
        .mode = 0,
        .spics_io_num = TOUCH_CS_PIN,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(SPI3_HOST, &devcfg, &s_touch_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
    }
}

static int touch_read_channel(uint8_t cmd) {
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = {0};

    spi_transaction_t t = {0};
    t.length = sizeof(tx) * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    esp_err_t err = spi_device_polling_transmit(s_touch_spi, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
        return 0;
    }

    return ((rx[1] << 8) | rx[2]) >> 3; // 12-bit result
}

bool touch_read_raw(int *x_raw, int *y_raw) {
    if (gpio_get_level(TOUCH_IRQ_PIN) != 0) {
        return false; // PENIRQ high = not touched
    }

    int x = touch_read_channel(XPT2046_CMD_READ_X);
    int y = touch_read_channel(XPT2046_CMD_READ_Y);

    if (x_raw) *x_raw = x;
    if (y_raw) *y_raw = y;
    return true;
}

bool touch_irq_active(void) {
    return gpio_get_level(TOUCH_IRQ_PIN) == 0;
}

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool touch_read_screen_xy(int *sx, int *sy) {
    int rx, ry;
    if (!touch_read_raw(&rx, &ry)) return false;

    int x = (rx - TOUCH_RAW_X_MIN) * TFT_WIDTH / (TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN);
    // Y is inverted on this panel: a raw reading near TOUCH_RAW_Y_MIN
    // corresponds to the BOTTOM of the screen, not the top.
    int y = (TOUCH_RAW_Y_MAX - ry) * TFT_HEIGHT / (TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN);

    if (sx) *sx = clamp(x, 0, TFT_WIDTH - 1);
    if (sy) *sy = clamp(y, 0, TFT_HEIGHT - 1);
    return true;
}
