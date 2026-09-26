#ifndef SPI_BUS_H
#define SPI_BUS_H

// Initializes the VSPI bus (SCK=18, MOSI=23, MISO=19) with DMA enabled,
// so devices can be added on it via spi_bus_add_device().
void spi_bus_init(void);

#endif
