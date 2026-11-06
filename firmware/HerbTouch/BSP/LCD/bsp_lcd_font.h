#ifndef __BSP_LCD_FONT_H
#define __BSP_LCD_FONT_H

#include <stdint.h>

extern const uint8_t g_ascii_font_1608[][16];
extern const uint8_t g_ascii_font_2412[][48];

typedef struct {
    uint32_t unicode;
    uint8_t bitmap[32];
} FontChinese16_t;

extern const FontChinese16_t g_chinese_font_1616[];
extern const uint16_t g_chinese_font_count;

const uint8_t* LCD_GetChineseFont16(uint32_t unicode);

#endif /* __BSP_LCD_FONT_H */
