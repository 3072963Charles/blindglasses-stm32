#ifndef URM09_V3_H
#define URM09_V3_H

#include "stm32h7xx_hal.h"

#define URM09_DEFAULT_ADDR    0x11

#define URM09_REG_DEV_ADDR    0x00
#define URM09_REG_PROD_ID     0x01
#define URM09_REG_VERSION     0x02
#define URM09_REG_DIST_H      0x03
#define URM09_REG_DIST_L      0x04
#define URM09_REG_TEMP_H      0x05
#define URM09_REG_TEMP_L      0x06
#define URM09_REG_CONFIG      0x07
#define URM09_REG_CMD         0x08

#define URM09_MODE_PASSIVE    0x00
#define URM09_MODE_AUTO       0x80

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t addr;
} URM09_V3_HandleTypeDef;

void URM09_V3_Init(URM09_V3_HandleTypeDef *h, I2C_HandleTypeDef *hi2c, uint8_t addr);
HAL_StatusTypeDef URM09_V3_SetAutoMode(URM09_V3_HandleTypeDef *h);
HAL_StatusTypeDef URM09_V3_ReadDistance(URM09_V3_HandleTypeDef *h, float *cm);

#endif
