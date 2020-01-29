#ifndef __I2C_H
#define __I2C_H

#define MAX_I2C_MSG_LEN		16
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

uint32_t rtlsdr_get_tuner_clock(void *dev);
int rtlsdr_i2c_write_fn(void *dev, uint8_t addr, uint8_t *buf, int len);
int rtlsdr_i2c_read_fn(void *dev, uint8_t addr, uint8_t *buf, int len);

#endif
