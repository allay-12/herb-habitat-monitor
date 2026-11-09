/* Captures real UI drawing calls using the production bitmap font tables. */
#include "bsp_lcd.h"
#include "bsp_lcd_font.h"
#include "lcd_capture.h"
#include <stdio.h>
#include <string.h>
static uint16_t pixels[320][480];
uint16_t TestLCD_Pixel(unsigned x,unsigned y){return x<480&&y<320?pixels[y][x]:0;}
unsigned test_lcd_missing_glyphs,test_lcd_out_of_bounds;
unsigned test_lcd_full_history_clears,test_lcd_chart_clears;
unsigned test_lcd_red_cursor_lines;
char test_lcd_point_detail[256];
char test_lcd_history_range[80];
void LCD_DrawPoint(uint16_t x,uint16_t y,uint16_t c)
{
    if(x>=480||y>=320){test_lcd_out_of_bounds++;return;}pixels[y][x]=c;
}
void LCD_Fill(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t c)
{
    if(x==0&&y==28&&w==480&&h==248)test_lcd_full_history_clears++;
    if(x==0&&y==145&&w==480&&h==130)test_lcd_chart_clears++;
    if(w==1&&h>=80&&c==LCD_COLOR_RED)test_lcd_red_cursor_lines++;
    for(unsigned j=y;j<(unsigned)y+h;j++)for(unsigned i=x;i<(unsigned)x+w;i++)LCD_DrawPoint(i,j,c);
}
void LCD_Clear(uint16_t c){LCD_Fill(0,0,480,320,c);}
void LCD_DrawHLine(uint16_t x,uint16_t y,uint16_t w,uint16_t c){LCD_Fill(x,y,w,1,c);}
void LCD_DrawVLine(uint16_t x,uint16_t y,uint16_t h,uint16_t c){LCD_Fill(x,y,1,h,c);}
void LCD_DrawRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t c)
{LCD_DrawHLine(x,y,w,c);LCD_DrawHLine(x,y+h-1,w,c);LCD_DrawVLine(x,y,h,c);LCD_DrawVLine(x+w-1,y,h,c);}
static void glyph(uint16_t x,uint16_t y,const uint8_t *data,unsigned w,unsigned h,uint16_t fg,uint16_t bg)
{
    unsigned bytes=(w+7)/8;
    for(unsigned j=0;j<h;j++)for(unsigned i=0;i<w;i++)LCD_DrawPoint(x+i,y+j,data[j*bytes+i/8]&(0x80>>(i%8))?fg:bg);
}
void LCD_ShowChar(uint16_t x,uint16_t y,char ch,uint8_t size,uint16_t fg,uint16_t bg)
{
    if(ch<32||ch>126)ch=' ';
    glyph(x,y,size==24?g_ascii_font_2412[ch-32]:g_ascii_font_1608[ch-32],size==24?12:8,size==24?24:16,fg,bg);
}
void LCD_ShowString(uint16_t x,uint16_t y,const char *s,uint8_t size,uint16_t fg,uint16_t bg)
{
    if(x==8&&y==123)snprintf(test_lcd_history_range,sizeof(test_lcd_history_range),"%s",s);
    if(x==8&&y==258)snprintf(test_lcd_point_detail,sizeof(test_lcd_point_detail),"%s",s);
    unsigned w=size==24?12:8;
    for(;*s&&x+w<=480;s++,x+=w)LCD_ShowChar(x,y,*s,size,fg,bg);
}
void LCD_ShowStringUTF8(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg)
{
    if(x==8&&y==258)snprintf(test_lcd_point_detail,sizeof(test_lcd_point_detail),"%s",s);
    const unsigned char *p=(const unsigned char*)s;
    while(*p) {
        if(*p<128){if(x+8>480)break;LCD_ShowChar(x,y,*p++,16,fg,bg);x+=8;continue;}
        unsigned n=(*p&0xe0)==0xc0?2:3;uint32_t code=*p&(n==2?31:15);
        for(unsigned i=1;i<n;i++)code=(code<<6)|(p[i]&63);
        const uint8_t *bitmap=LCD_GetChineseFont16(code);
        if(!bitmap){test_lcd_missing_glyphs++;printf("Missing capture glyph: U+%04X\n",(unsigned)code);}
        else if(x+16<=480)glyph(x,y,bitmap,16,16,fg,bg);
        x+=16;p+=n;
    }
}
void TestLCD_Save(const char *name)
{
    FILE *f=fopen(name,"wb");if(!f)return;
    fprintf(f,"P6\n480 320\n255\n");
    for(unsigned y=0;y<320;y++)for(unsigned x=0;x<480;x++) {
        uint16_t c=pixels[y][x];unsigned char rgb[3]={(unsigned char)(((c>>11)&31)*255/31),(unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
