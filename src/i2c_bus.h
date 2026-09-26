#ifndef I2C_BUS_H
#define I2C_BUS_H

void i2c_bus_init(void);

// Scans all 7-bit I2C addresses and logs which ones ACK.
// Diagnostic aid - call once after i2c_bus_init() to confirm a
// device is actually present/wired correctly before debugging it further.
void i2c_bus_scan(void);

#endif
