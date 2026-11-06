# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

idf_component_register(
    SRCS "main.c" "wifi.c" "ping.c" "timesync.c" "record.c" "i2c_bus.c" "oled.c" "font.c" "history.c" "epd_bitmap_data.c" "spi_bus.c" "tft.c" "touch.c" "ble_scan.c" "api_post.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_spi esp_driver_gpio lora bt nvs_flash esp_http_client
)