#include "bsp_lcd.h"
#include "bsp_lcd_font.h"
#include "tim.h"

static uint8_t  s_backlight_pct = 50;

static void LCD_WriteReg(uint16_t reg)
{
    LCD_REG = reg;
}

static void LCD_WriteData(uint16_t data)
{
    LCD_RAM = data;
}

static void LCD_HardwareReset(void)
{
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(25);
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(120);
}

void LCD_SetBacklight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_backlight_pct = percent;

    /* ARR = 999, map 0..100% to pulse 0..999 */
    uint32_t pulse = (uint32_t)percent * 999 / 100;
    __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, pulse);
    HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);
}

uint8_t LCD_GetBacklight(void)
{
    return s_backlight_pct;
}

static void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (x1 >= LCD_WIDTH)  x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;

    LCD_WriteReg(0x2A);
    LCD_WriteData(x0 >> 8);
    LCD_WriteData(x0 & 0xFF);
    LCD_WriteData(x1 >> 8);
    LCD_WriteData(x1 & 0xFF);

    LCD_WriteReg(0x2B);
    LCD_WriteData(y0 >> 8);
    LCD_WriteData(y0 & 0xFF);
    LCD_WriteData(y1 >> 8);
    LCD_WriteData(y1 & 0xFF);

    LCD_WriteReg(0x2C);
}

void LCD_Init(void)
{
    /* Keep backlight off during init to prevent white flash */
    __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, 0);

    LCD_HardwareReset();

    /* ST7796S Initialization Sequence */
    LCD_WriteReg(0x11);             /* Sleep OUT */
    HAL_Delay(120);

    /* Command Set Control */
    LCD_WriteReg(0xF0); LCD_WriteData(0xC3);
    LCD_WriteReg(0xF0); LCD_WriteData(0x96);

    /* Memory Access Control: 480x320 Landscape (MX=1, MV=1, BGR=1 -> 0x68) */
    LCD_WriteReg(0x36); LCD_WriteData(0x68);

    /* Interface Pixel Format: 16-bit/pixel (RGB565) */
    LCD_WriteReg(0x3A); LCD_WriteData(0x55);

    /* Display Inversion: 1-dot */
    LCD_WriteReg(0xB4); LCD_WriteData(0x01);

    /* Display Function Control */
    LCD_WriteReg(0xB6); LCD_WriteData(0x0A); LCD_WriteData(0xA2);

    /* Entry Mode */
    LCD_WriteReg(0xB7); LCD_WriteData(0xC6);

    /* Power Control */
    LCD_WriteReg(0xB9); LCD_WriteData(0x02); LCD_WriteData(0xE0);
    LCD_WriteReg(0xC0); LCD_WriteData(0x80); LCD_WriteData(0x16);
    LCD_WriteReg(0xC1); LCD_WriteData(0x19);
    LCD_WriteReg(0xC2); LCD_WriteData(0xA7);
    LCD_WriteReg(0xC5); LCD_WriteData(0x16);

    /* DOCA */
    LCD_WriteReg(0xE8);
    LCD_WriteData(0x40); LCD_WriteData(0x8A); LCD_WriteData(0x00); LCD_WriteData(0x00);
    LCD_WriteData(0x29); LCD_WriteData(0x19); LCD_WriteData(0xA5); LCD_WriteData(0x33);

    /* Positive Gamma */
    LCD_WriteReg(0xE0);
    LCD_WriteData(0xF0); LCD_WriteData(0x07); LCD_WriteData(0x0D); LCD_WriteData(0x04);
    LCD_WriteData(0x05); LCD_WriteData(0x14); LCD_WriteData(0x36); LCD_WriteData(0x54);
    LCD_WriteData(0x4C); LCD_WriteData(0x38); LCD_WriteData(0x13); LCD_WriteData(0x14);
    LCD_WriteData(0x2E); LCD_WriteData(0x34);

    /* Negative Gamma */
    LCD_WriteReg(0xE1);
    LCD_WriteData(0xF0); LCD_WriteData(0x10); LCD_WriteData(0x14); LCD_WriteData(0x0E);
    LCD_WriteData(0x0C); LCD_WriteData(0x08); LCD_WriteData(0x35); LCD_WriteData(0x44);
    LCD_WriteData(0x4C); LCD_WriteData(0x26); LCD_WriteData(0x10); LCD_WriteData(0x12);
    LCD_WriteData(0x2C); LCD_WriteData(0x32);

    /* Command Set Lock */
    LCD_WriteReg(0xF0); LCD_WriteData(0x3C);
    LCD_WriteReg(0xF0); LCD_WriteData(0x69);
    HAL_Delay(10);

    /* Display Inversion ON (IPS panel) */
    LCD_WriteReg(0x21);

    /* Display ON */
    LCD_WriteReg(0x29);
    HAL_Delay(25);

    /* Clear screen to white */
    LCD_Clear(LCD_COLOR_WHITE);

    /* Turn on backlight */
    LCD_SetBacklight(s_backlight_pct);
}

void LCD_Clear(uint16_t color)
{
    LCD_SetWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    uint32_t total = (uint32_t)LCD_WIDTH * LCD_HEIGHT;
    for (uint32_t i = 0; i < total; i++)
    {
        LCD_RAM = color;
    }
}

void LCD_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    LCD_SetWindow(x, y, x + w - 1, y + h - 1);
    uint32_t total = (uint32_t)w * h;
    for (uint32_t i = 0; i < total; i++)
    {
        LCD_RAM = color;
    }
}

void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    LCD_SetWindow(x, y, x, y);
    LCD_RAM = color;
}

void LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if (y >= LCD_HEIGHT || x >= LCD_WIDTH || w == 0) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    LCD_SetWindow(x, y, x + w - 1, y);
    for (uint16_t i = 0; i < w; i++)
    {
        LCD_RAM = color;
    }
}

void LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || h == 0) return;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    LCD_SetWindow(x, y, x, y + h - 1);
    for (uint16_t i = 0; i < h; i++)
    {
        LCD_RAM = color;
    }
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;
    LCD_DrawHLine(x, y, w, color);
    LCD_DrawHLine(x, y + h - 1, w, color);
    LCD_DrawVLine(x, y, h, color);
    LCD_DrawVLine(x + w - 1, y, h, color);
}

void LCD_DrawCross(uint16_t x, uint16_t y, uint16_t size, uint16_t color)
{
    if (x >= size) LCD_DrawHLine(x - size, y, size * 2 + 1, color);
    if (y >= size) LCD_DrawVLine(x, y - size, size * 2 + 1, color);
}

void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t size, uint16_t fc, uint16_t bc)
{
    if (ch < 32 || ch > 126) ch = ' ';
    uint8_t idx = ch - 32;

    if (size == 24)
    {
        /* 24x12 font: 24 rows, 2 bytes per row */
        if (x + 12 > LCD_WIDTH || y + 24 > LCD_HEIGHT) return;
        const uint8_t *bmp = g_ascii_font_2412[idx];
        LCD_SetWindow(x, y, x + 12 - 1, y + 24 - 1);
        for (uint8_t row = 0; row < 24; row++)
        {
            uint16_t word = ((uint16_t)bmp[row * 2] << 8) | bmp[row * 2 + 1];
            for (uint8_t col = 0; col < 12; col++)
            {
                if (word & (0x8000 >> col))
                {
                    LCD_RAM = fc;
                }
                else
                {
                    LCD_RAM = bc;
                }
            }
        }
    }
    else
    {
        /* 16x8 font */
        if (x + 8 > LCD_WIDTH || y + 16 > LCD_HEIGHT) return;
        const uint8_t *bmp = g_ascii_font_1608[idx];
        LCD_SetWindow(x, y, x + 8 - 1, y + 16 - 1);
        for (uint8_t row = 0; row < 16; row++)
        {
            uint8_t b = bmp[row];
            for (uint8_t col = 0; col < 8; col++)
            {
                if (b & (0x80 >> col))
                {
                    LCD_RAM = fc;
                }
                else
                {
                    LCD_RAM = bc;
                }
            }
        }
    }
}

void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint8_t size, uint16_t fc, uint16_t bc)
{
    uint16_t cur_x = x;
    uint8_t step = (size == 24) ? 12 : 8;
    while (*str)
    {
        if (cur_x + step > LCD_WIDTH) break;
        LCD_ShowChar(cur_x, y, *str, size, fc, bc);
        cur_x += step;
        str++;
    }
}

void LCD_ShowStringUTF8(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc)
{
    uint16_t cur_x = x;
    const uint8_t *p = (const uint8_t *)str;

    while (*p)
    {
        if (*p < 0x80)
        {
            /* ASCII */
            if (*p == '\n')
            {
                y += 18;
                cur_x = x;
                p++;
                continue;
            }
            if (cur_x + 8 > LCD_WIDTH) break;
            LCD_ShowChar(cur_x, y, (char)*p, 16, fc, bc);
            cur_x += 8;
            p++;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            /* 2-byte UTF-8 */
            uint32_t unicode = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
            const uint8_t *bmp = LCD_GetChineseFont16(unicode);
            if (bmp && (cur_x + 16 <= LCD_WIDTH) && (y + 16 <= LCD_HEIGHT))
            {
                LCD_SetWindow(cur_x, y, cur_x + 16 - 1, y + 16 - 1);
                for (uint8_t row = 0; row < 16; row++)
                {
                    uint16_t word = ((uint16_t)bmp[row * 2] << 8) | bmp[row * 2 + 1];
                    for (uint8_t col = 0; col < 16; col++)
                    {
                        LCD_RAM = (word & (0x8000 >> col)) ? fc : bc;
                    }
                }
            }
            cur_x += 16;
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            /* 3-byte UTF-8 (Standard Chinese character) */
            uint32_t unicode = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            const uint8_t *bmp = LCD_GetChineseFont16(unicode);
            if (bmp && (cur_x + 16 <= LCD_WIDTH) && (y + 16 <= LCD_HEIGHT))
            {
                LCD_SetWindow(cur_x, y, cur_x + 16 - 1, y + 16 - 1);
                for (uint8_t row = 0; row < 16; row++)
                {
                    uint16_t word = ((uint16_t)bmp[row * 2] << 8) | bmp[row * 2 + 1];
                    for (uint8_t col = 0; col < 16; col++)
                    {
                        LCD_RAM = (word & (0x8000 >> col)) ? fc : bc;
                    }
                }
            }
            else
            {
                /* Character not in subset: draw a small placeholder box */
                LCD_DrawRect(cur_x, y + 2, 14, 14, fc);
            }
            cur_x += 16;
            p += 3;
        }
        else
        {
            p++;
        }
    }
}
