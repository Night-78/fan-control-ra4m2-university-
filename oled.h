/*
 * oled.h - SSD1306 OLED 驱动（硬件 I2C 版）对外接口
 * 硬件: P408=I2C0_SCL, P407=I2C0_SDA, I2C 地址 0x3C, FSP 模块 g_i2c_master0
 *
 * FSP 配置检查清单（缺一不可）:
 *   1. r_iic_master 模块, Name = g_i2c_master0, Channel = 0
 *   2. Slave Address = 0x3C (7-bit), Rate = 400000 (fast mode)
 *   3. Callback 名必须填 i2c_master0_callback
 *   4. Pins: P407 -> IIC0 SDA, P408 -> IIC0 SCL（外设模式）
 *   5. R_IIC_MASTER_Open 全工程只调用一次（oled_init 内部已包含）
 */
#ifndef OLED_H_INCLUDED
#define OLED_H_INCLUDED

#include <stdint.h>

void oled_init(void);
void oled_clear(void);
void oled_draw_string(uint8_t page, uint8_t col, const char *str);

#endif /* OLED_H_INCLUDED */
