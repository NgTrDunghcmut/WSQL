# Wireless Signal Quality Logger (WSQL)

ESP32 / ESP-IDF firmware (built via PlatformIO) that turns a touchscreen-equipped
ESP32 board into a handheld signal-quality meter for three wireless links: WiFi,
BLE, and LoRa (SX1262). The user picks a mode on a home screen, the firmware runs
a live-updating readout for that link, and a back button returns to the mode
picker at any time.

## Hardware

- **MCU**: ESP32 (classic), 2MB flash, custom partition table (`partitions.csv`)
  grown to a 1.5MB app partition because the BLE/NimBLE stack pushed the app
  past the default 1MB factory partition.
- **Display**: ILI9341 TFT over SPI (`tft.c`/`tft.h`), replacing an earlier
  SSD1306/SSD1315 OLED (`oled.c`/`i2c_bus.c` — code kept but disabled in
  `main.c`).
- **Input**: XPT2046 resistive touch controller, sharing the TFT's SPI bus
  (`touch.c`). Level-based (`PENIRQ`), polled rather than interrupt-driven.
- **LoRa radio**: SX1262 (Ra-01S breakout, no onboard TCXO), vendored driver in
  `components/lora` — migrated this project from an original SX1276.
- Full pin-by-pin wiring is tracked separately in `PIN_MAPPING.md`.

## App flow

`app_main()` (`src/main.c`) initializes the display/touch, shows a welcome
screen, then loops forever: show the mode-select screen, block until a mode box
is tapped, run that mode's function to completion (it returns when the back
button is tapped), and loop back to mode-select.

```
app_main()
 └─ loop: tft_show_mode_select() → wait_for_mode_selection()
           ├─ 0: run_wifi_mode()
           ├─ 1: run_ble_mode()
           └─ 2: run_lora_mode()
```

Each mode's loop polls touch for the back button at high frequency (tens of ms)
so back-button response feels instant, while the actual sampling work happens
in the same loop at a slower cadence. WiFi and BLE additionally have real
background FreeRTOS tasks (the esp_wifi driver task, the NimBLE host task)
doing the underlying radio work; LoRa has no such background task — its mode
function polls the radio directly.

### WiFi mode (`run_wifi_mode`)

1. Connect to `WIFI_SSID`/`WIFI_PASS` (`pins_stats.h`), wait up to 10s.
2. Sync time via SNTP (`timesync_init`).
3. Let the user pick a ping target from a fixed list (Google DNS, Cloudflare,
   OpenDNS, local router — each with an optional fallback IP), verifying
   reachability before committing to it (`select_and_verify_ping_target`).
4. Loop once per second (`WIFI_SAMPLE_INTERVAL_MS`): ping the target
   (`ping_once`, ICMP, blocking up to 800ms via `SO_RCVTIMEO`), read WiFi RSSI,
   compute RTT, packet loss, and RFC 3550-style smoothed jitter (fixed-point,
   no floats), and push RSSI into a rolling-stats window. Display RSSI,
   latency, and jitter alongside rolling AVG/MAX/MIN.

### BLE mode (`run_ble_mode`)

1. Start scanning for named BLE devices (`ble_scan_init`).
2. Show a live, change-detected device list (redraws only when the set of
   discovered devices actually changes, to avoid flicker) capped at 7 entries
   (`BLE_SCAN_MAX_DEVICES`); user taps one to select it.
3. Connect, then poll RSSI every 500ms, feeding a rolling-stats window and
   showing live RSSI + AVG/MAX/MIN until the back button disconnects and
   returns.

### LoRa mode (`run_lora_mode`)

1. Init the SX1262 at `LORA_FREQUENCY_HZ`/`LORA_TX_POWER_DBM` (`pins_stats.h`),
   configure SF7/BW125/CR4·5 with CRC on, and arm continuous RX (no separate
   "start listening" call or re-arm between packets).
2. Poll for received packets every 20ms; on each packet, read RSSI/SNR, bump a
   packet counter, push RSSI into a rolling-stats window, and update the
   screen with RSSI/SNR/count + AVG/MAX/MIN. Pure polling — no background task.

## Rolling statistics

`rolling_stats.c`/`.h` is a small shared module used identically by all three
modes: a fixed-size ring buffer (`ROLLING_STATS_CAP = 100` samples, in
`pins_stats.h`) that recomputes AVG/MAX/MIN by scanning its current contents
on every `rolling_stats_get()` call. AVG divides by the actual sample count
while under 100, matching "last 100 samples, or fewer until warmed up."
Simple and cheap on CPU; the real cost is stack footprint for each mode's
struct against the tight `CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584` main-task
stack budget — acceptable today, but something to watch if this grows further
(stack-overflow canary is enabled, so a real overflow panics loudly rather
than corrupting memory silently).

## Configuration

`src/pins_stats.h` is the single source of truth for every GPIO assignment and
every user-tunable constant: SPI/I2C/touch pins, WiFi credentials and sample
interval, the ping-target IP list, LoRa frequency/TX power, the MQTT
broker URI/topic, and the rolling-stats window size. (Note: `PIN_MAPPING.md`
still refers to this file by its old name, `pins.h` — it was renamed to
`pins_stats.h` and the doc wasn't updated to match.)

SX1262 GPIO pins are the one exception — they're configured through
ESP-IDF's Kconfig system (`components/lora/Kconfig.projbuild`), with active
values baked into `sdkconfig.esp32dev` rather than `pins_stats.h`, since the
vendored LoRa driver reads them as `CONFIG_*` macros.

## MQTT publishing (built, not yet wired in)

`src/mqtt_pub.c`/`.h` wraps a vendored copy of `espressif/esp-mqtt`
(`components/mqtt/`, flattened into a single-SRCS CMake build; MQTT5 support
deliberately excluded) behind three functions: `mqtt_pub_init()`,
`mqtt_pub_is_connected()`, `mqtt_pub_send(payload)`. It replaced an earlier
HTTP-POST module (`api_post.c`, deleted). The broker address
(`MQTT_BROKER_URI` in `pins_stats.h`) is a placeholder (`mqtt://0.0.0.0:1883`)
pending a real IP.

**Currently unwired**: nothing in `main.c` calls into `mqtt_pub`, so
`--gc-sections` strips the entire component from the final binary (confirmed
by a temporary link-check: flash jumps from 67.6% to 74.8% — about +113KB —
the moment any `mqtt_pub_*` function is actually referenced). The vendored
component's Kconfig also defaults SSL and WebSocket transports to **on**
(`MQTT_TRANSPORT_SSL`, `MQTT_TRANSPORT_WEBSOCKET`, `_SECURE`) even though only
plain `mqtt://` is used — worth disabling in `sdkconfig.esp32dev` before
wiring this in, since those transports pull in mbedtls/esp-tls at real flash
cost for code paths this project never exercises.

`src/record.c`/`.h` (JSON record formatting) is currently unused — its only
call site was removed when `printf()` was stripped project-wide — but kept
around as a likely starting point for building the MQTT publish payload.

## Build

- PlatformIO (`platformio.ini`): `board = esp32dev`, `framework = espidf`,
  2MB flash, custom partition table.
- `src/CMakeLists.txt` lists all source files and component requirements
  (`driver`, `esp_driver_spi`, `esp_driver_gpio`, `lora`, `bt`, `nvs_flash`,
  `mqtt`).
- Logging currently uses `ESP_LOG*` only — all `printf()` calls (including
  ones that formatted JSON sample records) were removed project-wide to save
  flash; `ESP_LOG*` was deliberately kept.
- The build is currently compiled with `COMPILER_OPTIMIZATION_DEBUG`/
  `LEVEL_DEBUG` (`-Og`), not a size-optimized level — the largest remaining,
  unexploited flash-saving lever if space gets tight again.

## File map

| File | Responsibility |
|---|---|
| `main.c` | App entry point, mode dispatch, all three mode loops |
| `wifi.c`/`.h` | WiFi STA connect + RSSI read |
| `ping.c`/`.h` | Blocking ICMP ping (`ping_once`) |
| `timesync.c`/`.h` | SNTP time sync |
| `ble_scan.c`/`.h` | BLE scanning, device list, connect, RSSI read (NimBLE) |
| `ra01s.*` (in `components/lora`) | SX1262 LoRa driver |
| `rolling_stats.c`/`.h` | Shared AVG/MAX/MIN ring-buffer, used by all 3 modes |
| `tft.c`/`.h` | All TFT screens/widgets (mode select, per-mode stat screens, selection lists, back button) |
| `touch.c`/`.h` | XPT2046 touch read + calibration |
| `spi_bus.c`/`.h` | Shared SPI bus init for TFT + touch |
| `i2c_bus.c`/`.h`, `oled.c`/`.h` | Disabled OLED path, kept for reference |
| `pins_stats.h` | Central GPIO + tunable-constant definitions |
| `ui_theme.h` | Shared screen layout/style constants |
| `mqtt_pub.c`/`.h` | MQTT publish wrapper (built, unwired) |
| `record.c`/`.h` | JSON record formatting (currently unused) |
| `history.c`/`.h`, `font.c`/`.h`, `epd_bitmap_data.c`/`.h` | Supporting/legacy display assets |
