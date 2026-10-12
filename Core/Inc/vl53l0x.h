#ifndef VL53L0X_H
#define VL53L0X_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>

/* ---- I2C addresses ---- */
#define VL53L0X_DEFAULT_ADDR    0x29
#define VL53L0X_ADDR_BASE       0x30    // remapped to 0x30, 0x31, 0x32, 0x33

/* ---- Registers ---- */
#define VL53L0X_REG_SYSRANGE_START       0x0000
#define VL53L0X_REG_SYS_INT_CLEAR        0x000B
#define VL53L0X_REG_RESULT_INT_STATUS    0x0013
#define VL53L0X_REG_RESULT_RANGE_STATUS  0x0014
#define VL53L0X_REG_RESULT_RANGE_VAL     0x001E
#define VL53L0X_REG_VHV_CONFIG           0x0089
#define VL53L0X_REG_I2C_ADDR             0x008A

/* ---- Types ---- */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} VL53L0X_XshutPin;

/* ---- API ---- */
bool VL53L0X_InitAll(I2C_HandleTypeDef *hi2c,
                     const VL53L0X_XshutPin *xshut, int count);
void VL53L0X_ReadAll(I2C_HandleTypeDef *hi2c, uint16_t dist[4]);

#endif
