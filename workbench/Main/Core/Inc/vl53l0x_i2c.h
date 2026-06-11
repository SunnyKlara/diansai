/*
 * vl53l0x_i2c.h  -  software (bit-bang) I2C for VL53L0X on STM32H750
 *
 * Ported from the vendor (ALIENTEK) F103 reference and retargeted:
 *   - F1 "sys.h" dependency removed; uses HAL + config.h pin macros instead.
 *   - SCL/SDA moved to the pins freed by dropping the ultrasonic
 *     (see TOF_ defines in config.h: SCL=PD11, SDA=PD12).
 *   - All comments are pure ASCII. Do NOT put a slash-star-slash sequence or
 *     Chinese text inside block comments here: armclang decodes this file as
 *     GBK and a multibyte char abutting the comment terminator corrupts the
 *     build (see 04 debug log 2026-06-10).
 *
 * Signatures match the reference so vl53l0x_platform.c compiles unchanged.
 */
#ifndef __VL53L0X_I2C_H
#define __VL53L0X_I2C_H

#include "stm32h7xx_hal.h"
#include "config.h"

/* status codes used by the platform layer */
#define STATUS_OK       0x00
#define STATUS_FAIL     0x01

void    VL53L0X_i2c_init(void);                                              /* init SCL/SDA GPIO */
uint8_t VL53L0X_write_byte (uint8_t address, uint8_t index, uint8_t  data);
uint8_t VL53L0X_write_word (uint8_t address, uint8_t index, uint16_t data);
uint8_t VL53L0X_write_dword(uint8_t address, uint8_t index, uint32_t data);
uint8_t VL53L0X_write_multi(uint8_t address, uint8_t index, uint8_t *pdata, uint16_t count);
uint8_t VL53L0X_read_byte  (uint8_t address, uint8_t index, uint8_t  *pdata);
uint8_t VL53L0X_read_word  (uint8_t address, uint8_t index, uint16_t *pdata);
uint8_t VL53L0X_read_dword (uint8_t address, uint8_t index, uint32_t *pdata);
uint8_t VL53L0X_read_multi (uint8_t address, uint8_t index, uint8_t *pdata, uint16_t count);

#endif /* __VL53L0X_I2C_H */
