#ifndef HERB_LCD_CAPTURE_H
#define HERB_LCD_CAPTURE_H
#include <stdint.h>
uint16_t TestLCD_Pixel(unsigned x,unsigned y);
void TestLCD_Save(const char *path);
extern unsigned test_lcd_missing_glyphs,test_lcd_out_of_bounds;
extern unsigned test_lcd_full_history_clears,test_lcd_chart_clears;
extern unsigned test_lcd_red_cursor_lines;
extern char test_lcd_point_detail[256];
extern char test_lcd_history_range[80];
#endif
