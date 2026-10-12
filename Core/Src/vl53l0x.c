#include "vl53l0x.h"
#include <stdio.h>

#define TIMEOUT  100

/* ---- 8-bit register I2C helpers ---- */
static HAL_StatusTypeDef _wr8(I2C_HandleTypeDef *hi2c, uint8_t dev,
                               uint8_t reg, uint8_t val) {
    return HAL_I2C_Mem_Write(hi2c, dev << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, &val, 1, TIMEOUT);
}

static HAL_StatusTypeDef _rd8(I2C_HandleTypeDef *hi2c, uint8_t dev,
                               uint8_t reg, uint8_t *val) {
    return HAL_I2C_Mem_Read(hi2c, dev << 1, reg,
                            I2C_MEMADD_SIZE_8BIT, val, 1, TIMEOUT);
}

static HAL_StatusTypeDef _rd16(I2C_HandleTypeDef *hi2c, uint8_t dev,
                                uint8_t reg, uint16_t *val) {
    uint8_t buf[2] = {0};
    HAL_StatusTypeDef r = HAL_I2C_Mem_Read(hi2c, dev << 1, reg,
                            I2C_MEMADD_SIZE_8BIT, buf, 2, TIMEOUT);
    if (r == HAL_OK) *val = ((uint16_t)buf[0] << 8) | buf[1];
    return r;
}

/* ---- address remap ---- */
static bool _remap(I2C_HandleTypeDef *hi2c, uint8_t old_addr, uint8_t new_addr) {
    uint8_t val = new_addr & 0x7F;
    if (HAL_I2C_Mem_Write(hi2c, old_addr << 1, 0x8A, I2C_MEMADD_SIZE_8BIT,
                          &val, 1, TIMEOUT) != HAL_OK) return false;
    HAL_Delay(5);
    return (HAL_I2C_IsDeviceReady(hi2c, new_addr << 1, 2, 20) == HAL_OK);
}

/* ---- per-sensor init (lightweight) ---- */
static bool _init_one(I2C_HandleTypeDef *hi2c, uint8_t addr) {
    uint8_t v;
    /* read VHV config */
    if (_rd8(hi2c, addr, 0x89, &v) != HAL_OK) return false;
    /* set I2C standard mode */
    _wr8(hi2c, addr, 0x89, v | 0x01);
    /* clear interrupts */
    _wr8(hi2c, addr, 0x0B, 0x01);
    return true;
}

/* ---- read range (8-bit register clone: result at 0x1E-0x1F) ---- */
static uint16_t _range(I2C_HandleTypeDef *hi2c, uint8_t addr) {
    if (_wr8(hi2c, addr, 0x00, 0x01) != HAL_OK) return 0xFFFF;

    /* fixed delay: 0x13 bits 0-2 never set on this 8-bit clone */
    HAL_Delay(30);

    _wr8(hi2c, addr, 0x0B, 0x01);

    uint16_t d;
    /* primary result on this clone: 0x1E(H) 0x1F(L) */
    if (_rd16(hi2c, addr, 0x1E, &d) == HAL_OK && d > 4 && d <= 2000) return d;
    /* fallback: 0x15(H) 0x16(L) */
    if (_rd16(hi2c, addr, 0x15, &d) == HAL_OK && d > 4 && d <= 2000) return d;

    return 0xFFFF;
}

/* ================================================================
 * PUBLIC
 * ================================================================ */

bool VL53L0X_InitAll(I2C_HandleTypeDef *hi2c,
                     const VL53L0X_XshutPin *xshut, int count) {
    if (count <= 0 || count > 4) return false;

    /* 1. Reset all */
    for (int i = 0; i < count; i++)
        HAL_GPIO_WritePin(xshut[i].port, xshut[i].pin, GPIO_PIN_RESET);
    HAL_Delay(10);

    /* 2. Now proper init: remap all 4 sensors */
    for (int i = 0; i < count; i++) {
        uint8_t new_addr = VL53L0X_ADDR_BASE + i;

        HAL_GPIO_WritePin(xshut[i].port, xshut[i].pin, GPIO_PIN_SET);
        HAL_Delay(10);

        if (HAL_I2C_IsDeviceReady(hi2c, VL53L0X_DEFAULT_ADDR << 1, 3, 50) != HAL_OK) {
            printf("  Sensor %d not ready at 0x29!\r\n", i);
            return false;
        }

        if (!_remap(hi2c, VL53L0X_DEFAULT_ADDR, new_addr)) {
            printf("  Sensor %d remap failed!\r\n", i);
            return false;
        }
        printf("  Sensor %d: 0x29 -> 0x%02X\r\n", i, new_addr);
    }

    /* 4. Init each sensor */
    for (int i = 0; i < count; i++) {
        uint8_t addr = VL53L0X_ADDR_BASE + i;
        if (!_init_one(hi2c, addr)) {
            printf("  Sensor %d (0x%02X) init FAILED\r\n", i, addr);
            return false;
        }
        printf("  Sensor %d (0x%02X) OK\r\n", i, addr);
    }

    return true;
}

void VL53L0X_ReadAll(I2C_HandleTypeDef *hi2c, uint16_t dist[4]) {
    for (int i = 0; i < 4; i++) {
        uint16_t raw = _range(hi2c, VL53L0X_ADDR_BASE + i);
        dist[i] = (raw <= 2000) ? raw : 0;
    }
}
