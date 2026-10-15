#ifndef SSD1306_H
#define SSD1306_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define SSD1306_WIDTH 128U
#define SSD1306_HEIGHT 64U
#define SSD1306_I2C_ADDR (0x3CU << 1)

HAL_StatusTypeDef SSD1306_Init(I2C_HandleTypeDef *hi2c);
void SSD1306_Clear(void);
HAL_StatusTypeDef SSD1306_UpdateScreen(void);
void SSD1306_DrawPixel(uint8_t x, uint8_t y, uint8_t on);
void SSD1306_WriteChar(uint8_t x, uint8_t y, char c, uint8_t scale);
void SSD1306_WriteString(uint8_t x, uint8_t y, const char *text, uint8_t scale);

#endif
