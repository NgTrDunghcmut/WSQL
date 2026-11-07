#include "driver/spi_master.h"
#include "esp_log.h"
#include "spi_bus.h"
#include "pins_stats.h"

static const char *TAG = "SPIBus";

void spi_bus_init(void) {
    spi_bus_config_t buscfg = {
        .sclk_io_num = SPI_SCK_PIN,
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t err = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    }
}
