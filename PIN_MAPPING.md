# Pin Mapping

Every GPIO assignment in this firmware, by subsystem. The single source of truth in code is `src/pins.h` — this file is a readable reference alongside it; if they ever disagree, `src/pins.h` wins.

## Active — SPI bus (TFT + Touch)

The ILI9341 TFT and the XPT2046 touch controller share one physical SPI bus — `T_CLK`/`T_DIN`/`T_DO` on the touch panel's own silkscreen are literally the same wires/GPIOs as the TFT's `SCK`/`MOSI`/`MISO`, just labeled differently on each chip. Each device then has its own separate CS pin to tell them apart.

| Signal | GPIO | Defined in | Notes |
|---|---|---|---|
| SCK / **T_CLK** | **5** | `pins.h` → `SPI_SCK_PIN` | Shared bus clock — wire the panel's `T_CLK` here too |
| MOSI / **T_DIN** | **18** | `pins.h` → `SPI_MOSI_PIN` | Shared bus data out — wire the panel's `T_DIN` here too |
| MISO / **T_DO** | **22** | `pins.h` → `SPI_MISO_PIN` | Shared bus data in — wire the panel's `T_DO` here too |
| TFT CS | **23** | `pins.h` → `TFT_CS_PIN` | Passed to the SPI driver; not toggled manually in code |
| TFT DC/RS | **19** | `pins.h` → `TFT_DC_PIN` | LOW = command, HIGH = data |
| TFT RST | **21** | `pins.h` → `TFT_RST_PIN` | Active-low reset pulse |
| **T_CS** (touch CS) | **32** | `pins.h` → `TOUCH_CS_PIN` | Dedicated pin, not shared |
| **T_IRQ** (touch IRQ) | **35** | `pins.h` → `TOUCH_IRQ_PIN` | Dedicated pin, not shared. Active-low when panel is touched (PENIRQ) |

So all five XPT2046 touch pins are accounted for: `T_CLK`→5, `T_DIN`→18, `T_DO`→22 (shared with the TFT), plus `T_CS`→32 and `T_IRQ`→35 (dedicated).

**Bus speeds:** TFT device at `SPI_MASTER_FREQ_10M` (10 MHz — lowered from an initial 20 MHz for reliability over breadboard jumper wiring). Touch device at 1 MHz (comfortably under the XPT2046's spec).

**TFT power (not GPIO):**
- VCC → 5V (module has an onboard regulator per its datasheet; confirmed safe for 5V input)
- GND → common ground
- LED (backlight) → tied directly to 3.3V, no GPIO control (not PWM-dimmed)

**Touch calibration** (`touch.c`, raw ADC → screen pixels):
- X: raw 200–3900 maps directly to 0–`TFT_WIDTH`
- Y: **inverted** — raw 200–3900 maps to `TFT_HEIGHT`–0 (confirmed via a 4-corner tap test; this panel's Y axis runs opposite to the display's)

## Disabled — I2C bus (OLED display)

Currently commented out in `main.c` (the project switched from an SSD1306/SSD1315 OLED to the SPI TFT). Code is intact in `i2c_bus.c`/`oled.c` if re-enabled later.

| Signal | GPIO | Defined in | Notes |
|---|---|---|---|
| SDA | **21** | `pins.h` → `I2C_SDA_PIN` | ⚠️ same GPIO as active `TFT_RST_PIN` above |
| SCL | **22** | `pins.h` → `I2C_SCL_PIN` | ⚠️ same GPIO as active `SPI_MISO_PIN` above |

**Bus speed:** 100 kHz (lowered from 400 kHz — ESP32's internal pull-ups were too weak for reliable 400 kHz signaling over breadboard wiring).

**OLED address:** `0x3C` (defined locally in `oled.c`, not in `pins.h`, since it's an I2C device address rather than a GPIO assignment).

> **GPIO21 and GPIO22 are reused** between the disabled I2C path and the active SPI/TFT path. This is harmless as long as the OLED stays disabled — re-enabling `i2c_bus_init()`/`oled_init()` while the TFT is also active would conflict, since GPIO21 can't simultaneously be TFT reset and I2C SDA.

## ESP32 pin constraints that shaped these choices

- **GPIO34/35/36/39 are input-only** on the classic ESP32 (no output driver, no internal pull-up/pull-down). This is why `TOUCH_IRQ` (read-only) uses GPIO35, but an earlier attempt to put `TOUCH_CS` (must be driven low by the MCU) on GPIO34 had to be moved to GPIO32.
- **GPIO1/GPIO3** are UART0 TX/RX — reserved for flashing and the serial monitor, never repurposed here.
- **GPIO0/2/4/5/12/15** are strapping pins (sampled at boot/reset to select boot mode). Used here anyway (`SPI_SCK_PIN`=5, `TFT_DC_PIN` history touched GPIO2 earlier) since none of them are pulled against their required boot-time level by anything on this board.
