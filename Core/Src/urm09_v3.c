#include "urm09_v3.h"
#include <stdio.h>

static const char *_hal_err(uint32_t e) {
    switch (e) {
    case HAL_OK:      return "OK";
    case HAL_ERROR:   return "ERROR";
    case HAL_BUSY:    return "BUSY";
    case HAL_TIMEOUT: return "TIMEOUT";
    default:          return "UNKNOWN";
    }
}

void URM09_V3_Init(URM09_V3_HandleTypeDef *h, I2C_HandleTypeDef *hi2c, uint8_t addr) {
    h->hi2c = hi2c;
    h->addr = addr;
}

HAL_StatusTypeDef URM09_V3_SetAutoMode(URM09_V3_HandleTypeDef *h) {
    uint8_t dev = h->addr << 1;

    /* probe device first */
    HAL_StatusTypeDef s = HAL_I2C_IsDeviceReady(h->hi2c, dev, 2, 100);
    printf("  [DBG] IsDeviceReady(0x%02X): %s\r\n", h->addr, _hal_err(s));
    if (s != HAL_OK) return s;

    /* try product ID read to verify it's a URM09 */
    uint8_t pid = 0;
    s = HAL_I2C_Mem_Read(h->hi2c, dev, URM09_REG_PROD_ID,
                         I2C_MEMADD_SIZE_8BIT, &pid, 1, 100);
    printf("  [DBG] Product ID read: %s, value=0x%02X\r\n", _hal_err(s), pid);

    /* write CONFIG = AUTO mode */
    uint8_t val = URM09_MODE_AUTO;
    s = HAL_I2C_Mem_Write(h->hi2c, dev, URM09_REG_CONFIG,
                          I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
    printf("  [DBG] Mem_Write CONFIG=0x%02X: %s\r\n", URM09_MODE_AUTO, _hal_err(s));
    return s;
}

HAL_StatusTypeDef URM09_V3_ReadDistance(URM09_V3_HandleTypeDef *h, float *cm) {
    uint8_t data[2] = {0};
    HAL_StatusTypeDef s = HAL_I2C_Mem_Read(h->hi2c, h->addr << 1,
                            URM09_REG_DIST_H, I2C_MEMADD_SIZE_8BIT, data, 2, 100);
    if (s != HAL_OK) {
        static int fail_cnt = 0;
        if (++fail_cnt < 5) printf("  [URM] Rd fail: %s\r\n", _hal_err(s));
        *cm = 0; return s;
    }
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    if (raw < 2 || raw > 500) {
        static int range_cnt = 0;
        if (++range_cnt < 5) printf("  [URM] range out: raw=%u\r\n", raw);
        *cm = 0; return HAL_ERROR;
    }
    *cm = (float)raw;
    return HAL_OK;
}
