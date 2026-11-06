/* Real SD append -> production history -> live chart; no fake reader. */
#include "ui_history.c"
#include "node_store.h"
#include "node_identity.h"
#include "disk_image.h"
#include "lcd_capture.h"
static uint32_t now;static unsigned failures;
#define CHECK(c,s) do{if(!(c)){printf("FAIL: %s\n",s);failures++;}}while(0)
uint32_t HAL_GetTick(void){return now;}
static uint32_t stamp(const char *s){uint32_t t=0;HistoryDate_Parse(s,&t);return t;}
static void finish(void){for(unsigned i=0;i<10000;i++){HistoryUI_Process(now+=10);if(!reading(History_Get()->status))return;}CHECK(0,"query terminates");}
static void save(const char *id,const char *time,float temp,uint32_t key)
{NodeRecord r;AirReading a={(int16_t)(temp*10),650,123};CHECK(NodeRecord_BuildSensors(&r,id,&a,0,stamp(time))&&NodeStore_Save(&r,key),"append actual CSV and durable outbox");}
static void click(int x,int y){TouchEvent_t e={TOUCH_EVT_DOWN,x,y};HistoryUI_Touch(&e);e.type=TOUCH_EVT_UP;HistoryUI_Touch(&e);}
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    TestDisk_SetImage(argv[1]);TestDisk_AllowWrites(1);HistoryUI_Init();
    save("live-first","2026-11-04 21:00:28",22.3f,701);
    HistoryUI_Enter();finish();const HistorySeries *s=History_Get();
    CHECK(s->status==HISTORY_READY&&s->rows==1,"first actual sample visible");
    CHECK(s->end-s->start==3600,"hour view is recent one hour, not an unlabeled cross-day 24-hour span");
    unsigned first=0;while(first<s->points&&!s->point[first].count)first++;
    CHECK(first<s->points&&s->point[first].first_stamp==stamp("2026-11-04 21:00:28"),"point retains acquisition timestamp instead of bucket start");
    unsigned lines=test_lcd_red_cursor_lines,clears=test_lcd_chart_clears;
    click(PLOT_X+PLOT_W-4,PLOT_Y+PLOT_H/2);
    CHECK(test_lcd_red_cursor_lines==lines,"point selection must not draw a full-height red cursor");
    CHECK(test_lcd_chart_clears==clears,"point selection updates marker and tooltip without clearing the plot");
    CHECK(strstr(test_lcd_point_detail,"2026-11-04 21:00:28")&&strstr(test_lcd_point_detail,"22.3"),"selected point draws its actual acquisition time to seconds and value");
    CHECK(strstr(test_lcd_point_detail,"时间：")&&strstr(test_lcd_point_detail,"数值："),"selected point has readable Chinese labels");
    char image[1024];snprintf(image,sizeof(image),"%s/live-selected.ppm",argv[2]);TestLCD_Save(image);
    save("live-second","2026-11-04 21:01:28",23.1f,702);
    HistoryUI_Process(now+=1500);finish();s=History_Get();
    CHECK(s->rows==2,"new SD sample refreshes the visible chart without another button press");
    unsigned populated=0;for(unsigned i=0;i<s->points;i++)if(s->point[i].count)populated++;
    CHECK(populated==2,"consecutive minute samples produce two curve points");
    CHECK(s->end==stamp("2026-11-04 21:02:00"),"live hour range follows the newest record");
    unsigned new_ink=0;for(unsigned x=PLOT_X+PLOT_W-10;x<PLOT_X+PLOT_W;x++)if(TestLCD_Pixel(x,PLOT_Y)==LCD_COLOR_BLUE)new_ink++;
    CHECK(new_ink>0,"new point is actually painted even when the SD query finishes within one Process call");
    CHECK(test_lcd_chart_clears==clears,"automatic append must not blank the chart before painting the new point");
    snprintf(image,sizeof(image),"%s/live-appended.ppm",argv[2]);TestLCD_Save(image);
    unsigned reads=test_disk_reads;for(unsigned i=0;i<100;i++)HistoryUI_Process(now+=300);
    CHECK(test_disk_reads==reads,"no repeated SD scan when no new data arrives");
    click(270,52);HistoryDate_FromSeconds(stamp("2026-11-04 21:00:00"),&edit[0]);
    HistoryDate_FromSeconds(stamp("2026-11-04 21:01:00"),&edit[1]);edit_unit=HISTORY_HOUR;click(390,250);finish();
    uint32_t a=s->start,b=s->end;save("live-third","2026-11-04 21:02:28",24.0f,703);
    HistoryUI_Process(now+=1500);finish();
    CHECK(s->start==a&&s->end==b&&s->rows==1,"custom range stays fixed when a newer record arrives");
    save("live-quarter","2026-11-04 21:17:28",25.0f,704);
    HistoryUI_SelectMetric(0);HistoryUI_Enter();finish();s=History_Get();
    unsigned earlier=0,later=0;
    for(unsigned i=0;i<s->points;i++)if(s->point[i].count) {
        if(s->point[i].first_stamp==stamp("2026-11-04 21:02:28"))earlier=i;
        if(s->point[i].first_stamp==stamp("2026-11-04 21:17:28"))later=i;
    }
    int middle=(point_x(s,earlier)+point_x(s,later))/2;
    unsigned ink=0;
    for(int y=PLOT_Y;y<PLOT_Y+PLOT_H;y++)
        if(TestLCD_Pixel((unsigned)middle,(unsigned)y)==LCD_COLOR_BLUE)ink++;
    CHECK(ink>0,"real SD writes fifteen minutes apart form a connected curve");
    save("live-missed-cycle","2026-11-04 21:47:28",26.0f,705);
    HistoryUI_Process(now+=1500);finish();s=History_Get();
    for(unsigned i=0;i<s->points;i++)if(s->point[i].count) {
        if(s->point[i].first_stamp==stamp("2026-11-04 21:17:28"))earlier=i;
        if(s->point[i].first_stamp==stamp("2026-11-04 21:47:28"))later=i;
    }
    middle=(point_x(s,earlier)+point_x(s,later))/2;ink=0;
    for(int y=PLOT_Y;y<PLOT_Y+PLOT_H;y++)
        if(TestLCD_Pixel((unsigned)middle,(unsigned)y)==LCD_COLOR_BLUE)ink++;
    CHECK(ink==0,"a real missed thirty-minute interval remains a gap");
    CHECK(!test_lcd_missing_glyphs&&!test_lcd_out_of_bounds,"live plot remains within framebuffer");
    printf("Live SD/chart/selection regression failures: %u\n",failures);return failures?1:0;
}
