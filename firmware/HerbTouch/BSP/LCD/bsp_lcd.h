#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#include "main.h"
#include <stdint.h>

/* FSMC NE1/A16 Direct Register/RAM Access */
#define LCD_REG                 (*((volatile uint16_t *)0x60000000))
#define LCD_RAM                 (*((volatile uint16_t *)0x60020000))

/* Dimensions in Landscape */
#define LCD_WIDTH               480
#define LCD_HEIGHT              320

/* Common RGB565 Colors */
#define LCD_COLOR_WHITE         0xFFFF
#define LCD_COLOR_BLACK         0x0000
#define LCD_COLOR_BLUE          0x001F
#define LCD_COLOR_GREEN         0x07E0
#define LCD_COLOR_RED           0xF800
#define LCD_COLOR_YELLOW        0xFFE0

/* UI Theme Colors */
#define LCD_COLOR_DARK_TEXT     0x18C3  /* #171717 */
#define LCD_COLOR_BG_GRAY       0xF7BE  /* #F7F7F7 */
#define LCD_COLOR_BAR_GRAY      0xEF7D  /* #EEEEEE */
#define LCD_COLOR_BORDER_GRAY   0xC618  /* #C7C7C7 */
#define LCD_COLOR_TEXT_MUTED    0x632C  /* #666666 */
#define LCD_COLOR_TEXT_DIM      0x52AA  /* #555555 */
#define LCD_COLOR_BTN_DIS_BG    0xF79E  /* #F4F4F4 */
#define LCD_COLOR_BTN_DIS_TXT   0x738E  /* #777777 */

/* Public API */
void     LCD_Init(void);
uint16_t LCD_ReadID(void);
void     LCD_SetBacklight(uint8_t percent);
uint8_t  LCD_GetBacklight(void);
void     LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void     LCD_Clear(uint16_t color);
void     LCD_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void     LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void     LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void     LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     LCD_DrawCross(uint16_t x, uint16_t y, uint16_t size, uint16_t color);

void     LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t size, uint16_t fc, uint16_t bc);
void     LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t size, uint16_t fc, uint16_t bc);
void     LCD_ShowStringUTF8(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc);

#endif /* __BSP_LCD_H */
