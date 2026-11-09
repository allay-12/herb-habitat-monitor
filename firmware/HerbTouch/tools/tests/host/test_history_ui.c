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
    const char *tabs[]={"概览","土壤","历史","设置"};
    for(unsigned i=0;i<4;i++) {
        uint16_t bg=i==2?LCD_COLOR_DARK_TEXT:LCD_COLOR_WHITE,fg=i==2?LCD_COLOR_WHITE:LCD_COLOR_DARK_TEXT;
        LCD_Fill(i*120,276,120,44,bg);LCD_ShowStringUTF8(i*120+44,290,tabs[i],fg,bg);
    }
    TestLCD_Save(path);
}
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    TestDisk_SetImage(argv[1]);HistoryUI_Init();HistoryUI_Enter();finish();
    CHECK(History_Get()->status==HISTORY_READY&&History_Get()->rows==4&&History_Get()->raw&&History_Get()->points==4,"hour preset reads each raw record in the last hour");save(argv[2],"history-hour");
    const HistorySeries *hour=History_Get();
    unsigned first=0,second=0;
    while(first<hour->points&&!hour->point[first].count)first++;
    second=first+1;
    while(second<hour->points&&!hour->point[second].count)second++;
    CHECK(second<hour->points,"fifteen-minute samples are present on the hour chart");
    CHECK(point_x(hour,first)==PLOT_X,"first actual sample starts at the left edge");
    if(second<hour->points) {
        int middle=(point_x(hour,first)+point_x(hour,second))/2;
        bool connected=false;
        for(int y=PLOT_Y;y<PLOT_Y+PLOT_H;y++)
            if(TestLCD_Pixel((unsigned)middle,(unsigned)y)==LCD_COLOR_BLUE)connected=true;
        CHECK(connected,"hour chart connects actual samples fifteen minutes apart across empty minute bins");
    }
    click(102,98);finish();CHECK(unit==HISTORY_DAY&&History_Get()->end-History_Get()->start==86400&&History_Get()->status==HISTORY_READY&&!History_Get()->raw&&History_Get()->unit==HISTORY_HOUR,"24-hour preset aggregates 96 records by exact hours");save(argv[2],"history-day");
    CHECK(point_x(History_Get(),0)==PLOT_X,"first populated hourly aggregate uses its real first sample as x origin");
    click(166,98);finish();CHECK(unit==HISTORY_MONTH&&History_Get()->end-History_Get()->start==30u*86400u&&History_Get()->unit==HISTORY_DAY&&History_Get()->status==HISTORY_READY,"30-day preset selects daily means after 72 occupied hours");save(argv[2],"history-month");
    CHECK(plot_hi-plot_lo<2,"daily mean chart scales to displayed means instead of hidden intra-day extremes");
    click(230,98);finish();CHECK(unit==HISTORY_YEAR&&History_Get()->end-History_Get()->start==365u*86400u&&History_Get()->unit==HISTORY_DAY,"year button queries a local 365-day window");save(argv[2],"history-year");
    uint32_t old_start=History_Get()->start,old_end=History_Get()->end;
    click(310,98);finish();CHECK(!follow_latest&&History_Get()->adaptive&&History_Get()->view==HISTORY_YEAR&&History_Get()->end==old_start,"previous segment retains year adaptive rules");
    click(415,98);finish();CHECK(History_Get()->start==old_start&&History_Get()->end==old_end&&History_Get()->rows>0,"next segment restores fixed year bounds and full records");
    click(100,52);CHECK(mode==1,"metric selector opens");save(argv[2],"history-metrics");
    click(330,98);finish();CHECK(metric==3&&History_Get()->metric==3,"soil temperature selection queries actual column");
    click(270,52);CHECK(mode==2,"date range editor opens");
    save(argv[2],"history-date-editor");
    HistoryDate a={2026,10,7,6,0,0},b={2026,10,8,18,0,0};edit[0]=a;edit[1]=b;edit_unit=HISTORY_HOUR;
    click(390,250);finish();
    CHECK(mode==0&&History_Get()->rows==144&&History_Get()->points==36,"custom start/end times and hourly aggregation applied");save(argv[2],"history-custom");
    click(270,52);edit[1]=edit[0];click(390,250);CHECK(mode==2,"reversed/equal editor range rejected");
    edit[1]=b;edit_unit=HISTORY_MONTH;click(50,250);CHECK(mode==0&&unit==HISTORY_HOUR,"cancel does not change active aggregation");
    uint32_t clock;HistoryDate_Parse("2026-10-08 19:00:30",&clock);History_SetTime(clock);
    click(270,52);edit[1]=(HistoryDate){2026,10,8,20,0,0};
    click(390,250);CHECK(mode==2,"future custom endpoint cannot be applied");
    edit[1]=(HistoryDate){2026,10,8,19,0,0};edit_end=1;edit_field=4;
    click(420,198);CHECK(edit[1].minute==0,"date increment cannot step into a future minute");
    CHECK(TestLCD_Pixel(374,180)==LCD_COLOR_BAR_GRAY,"future increment is visibly greyed out");
    click(50,250);History_SetTime(0);
    TouchEvent_t down={TOUCH_EVT_DOWN,100,52},move={TOUCH_EVT_MOVE,270,52},up={TOUCH_EVT_UP,270,52};
    HistoryUI_Touch(&down);HistoryUI_Touch(&move);HistoryUI_Touch(&up);CHECK(mode==0,"drag to another control does not activate it");
    CHECK(test_lcd_missing_glyphs==0,"all history UI text has generated glyphs");
    CHECK(test_lcd_out_of_bounds==0,"all history draw calls stay inside framebuffer");
    CHECK(test_disk_writes==0,"history UI never writes to card");
    CHECK(point_y(3e38f,-3e38,3e38)>=PLOT_Y && point_y(-3e38f,-3e38,3e38)<=PLOT_Y+PLOT_H,"finite extreme values cannot overflow chart scaling");
    char label[32];axis_text(label,sizeof(label),-3e38);CHECK(strlen(label)<=7,"large axis label stays inside left margin");
    HistoryUI_Leave();
    TestDisk_SetImage("missing-card.img");HistoryUI_Init();unit=HISTORY_YEAR;HistoryUI_Enter();finish();
    CHECK(History_Get()->status==HISTORY_NO_CARD,"year preset handles missing SD before time is known");
    click(270,52);CHECK(mode==2&&edit_unit==HISTORY_HOUR,"no-card year preset still opens a supported custom grain");
    HistoryUI_Leave();printf("History UI regression failures: %u\n",failures);return failures?1:0;
}
