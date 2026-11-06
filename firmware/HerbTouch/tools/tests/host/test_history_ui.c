#include "ui_history.c"
#include "disk_image.h"
#include "lcd_capture.h"
#include <stdio.h>
static unsigned failures;static uint32_t now;
#define CHECK(c,msg) do {if(!(c)){printf("FAIL: %s\n",msg);failures++;}} while(0)
uint32_t HAL_GetTick(void){return now;}
static void finish(void)
{
    for(unsigned i=0;i<10000;i++){now+=10;HistoryUI_Process(now);if(History_Get()->status!=HISTORY_READING&&History_Get()->status!=HISTORY_FINDING)return;}
    CHECK(0,"history UI query timeout");
}
static void click(int x,int y)
{
    TouchEvent_t e={TOUCH_EVT_DOWN,x,y};HistoryUI_Touch(&e);e.type=TOUCH_EVT_UP;HistoryUI_Touch(&e);
}
static void save(const char *folder,const char *name)
{
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",folder,name);
    LCD_Fill(0,0,480,28,LCD_COLOR_BAR_GRAY);LCD_ShowStringUTF8(12,6,"本地SD卡历史数据",LCD_COLOR_DARK_TEXT,LCD_COLOR_BAR_GRAY);
    const char *tabs[]={"概览","土壤","历史","设置","诊断"};
    for(unsigned i=0;i<5;i++) {
        uint16_t bg=i==2?LCD_COLOR_DARK_TEXT:LCD_COLOR_WHITE,fg=i==2?LCD_COLOR_WHITE:LCD_COLOR_DARK_TEXT;
        LCD_Fill(i*96,276,96,44,bg);LCD_ShowStringUTF8(i*96+32,290,tabs[i],fg,bg);
    }
    TestLCD_Save(path);
}
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    TestDisk_SetImage(argv[1]);HistoryUI_Init();HistoryUI_Enter();finish();
    CHECK(History_Get()->status==HISTORY_READY&&History_Get()->rows==4,"hour preset reads recent hour of SD demo with minute resolution");save(argv[2],"history-hour");
    const HistorySeries *hour=History_Get();
    unsigned first=0,second=0;
    while(first<hour->points&&!hour->point[first].count)first++;
    second=first+1;
    while(second<hour->points&&!hour->point[second].count)second++;
    CHECK(second<hour->points,"fifteen-minute samples are present on the hour chart");
    if(second<hour->points) {
        int middle=(point_x(hour,first)+point_x(hour,second))/2;
        bool connected=false;
        for(int y=PLOT_Y;y<PLOT_Y+PLOT_H;y++)
            if(TestLCD_Pixel((unsigned)middle,(unsigned)y)==LCD_COLOR_BLUE)connected=true;
        CHECK(connected,"hour chart connects actual samples fifteen minutes apart across empty minute bins");
    }
    click(128,98);finish();CHECK(unit==HISTORY_DAY&&History_Get()->end-History_Get()->start==86400&&History_Get()->status==HISTORY_READY,"day preset plots recent 24 hours");save(argv[2],"history-day");
    click(212,98);finish();CHECK(unit==HISTORY_MONTH&&History_Get()->points==30&&History_Get()->status==HISTORY_READY,"month preset plots recent 30 days");save(argv[2],"history-month");
    click(100,52);CHECK(mode==1,"metric selector opens");save(argv[2],"history-metrics");
    click(330,98);finish();CHECK(metric==3&&History_Get()->metric==3,"soil temperature selection queries actual column");
    click(270,52);CHECK(mode==2,"date range editor opens");
    save(argv[2],"history-date-editor");
    HistoryDate a={2026,10,7,6,0,0},b={2026,10,8,18,0,0};edit[0]=a;edit[1]=b;edit_unit=HISTORY_HOUR;
    click(390,250);finish();
    CHECK(mode==0&&History_Get()->rows==144&&History_Get()->points==36,"custom start/end times and hourly aggregation applied");save(argv[2],"history-custom");
    click(270,52);edit[1]=edit[0];click(390,250);CHECK(mode==2,"reversed/equal editor range rejected");
    edit[1]=b;edit_unit=HISTORY_MONTH;click(50,250);CHECK(mode==0&&unit==HISTORY_HOUR,"cancel does not change active aggregation");
    TouchEvent_t down={TOUCH_EVT_DOWN,100,52},move={TOUCH_EVT_MOVE,270,52},up={TOUCH_EVT_UP,270,52};
    HistoryUI_Touch(&down);HistoryUI_Touch(&move);HistoryUI_Touch(&up);CHECK(mode==0,"drag to another control does not activate it");
    CHECK(test_lcd_missing_glyphs==0,"all history UI text has generated glyphs");
    CHECK(test_lcd_out_of_bounds==0,"all history draw calls stay inside framebuffer");
    CHECK(test_disk_writes==0,"history UI never writes to card");
    CHECK(point_y(3e38f,-3e38,3e38)>=PLOT_Y && point_y(-3e38f,-3e38,3e38)<=PLOT_Y+PLOT_H,"finite extreme values cannot overflow chart scaling");
    char label[32];axis_text(label,sizeof(label),-3e38);CHECK(strlen(label)<=7,"large axis label stays inside left margin");
    HistoryUI_Leave();printf("History UI regression failures: %u\n",failures);return failures?1:0;
}
