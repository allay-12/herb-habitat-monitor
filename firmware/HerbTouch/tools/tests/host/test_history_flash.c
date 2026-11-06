/* Actual UI callbacks + real FatFs; no mirrored query/render implementation. */
#include "ui_history.c"
#include "history_fs.h"
#include "disk_image.h"
#include "lcd_capture.h"
#include <stdio.h>
static uint32_t now;static unsigned failures;
#define CHECK(c,s) do{if(!(c)){printf("FAIL: %s\n",s);failures++;}}while(0)
uint32_t HAL_GetTick(void){return now;}
static void click(int x,int y)
{TouchEvent_t e={TOUCH_EVT_DOWN,x,y};HistoryUI_Touch(&e);e.type=TOUCH_EVT_UP;HistoryUI_Touch(&e);}
static void finish(void)
{
    for(unsigned i=0;i<20000;i++) {
        if(History_Get()->status!=HISTORY_READING&&History_Get()->status!=HISTORY_FINDING)return;
        now+=300;HistoryUI_Process(now);
    }
    CHECK(0,"query must terminate");
}
int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    TestDisk_SetImage(argv[1]);HistoryUI_Init();HistoryUI_Enter();finish();
    CHECK(History_Get()->status==HISTORY_READY,"sparse real FAT fixture loaded");
    unsigned reads=test_disk_reads;
    click(212,98); /* Recent month with one actual day on card. */
    unsigned clears=test_lcd_full_history_clears,chart=test_lcd_chart_clears;
    uint32_t generation=HistoryFS_Generation();
    finish();
    printf("SPARSE_YEAR disk_reads=%u full_page_repaints=%u plot_clears=%u mounts=%lu\n",test_disk_reads-reads,
        test_lcd_full_history_clears-clears,test_lcd_chart_clears-chart,(unsigned long)(HistoryFS_Generation()-generation));
    CHECK(test_disk_reads-reads<=128,"year query must enumerate existing files instead of probing every absent day");
    CHECK(test_lcd_full_history_clears==clears,"query progress must not blank/repaint the whole history page");
    CHECK(HistoryFS_Generation()==generation,"query must not restart its mount during polling");
    CHECK(History_Get()->status==HISTORY_READY&&History_Get()->rows==1,"sparse year result remains one real sample");
    reads=test_disk_reads;clears=test_lcd_full_history_clears;
    for(unsigned i=0;i<100;i++){now+=300;HistoryUI_Process(now);}
    CHECK(test_disk_reads==reads&&test_lcd_full_history_clears==clears,"finished query stays idle");
    click(270,52);CHECK(mode==2,"date editor opens");
    clears=test_lcd_full_history_clears;reads=test_disk_reads;generation=HistoryFS_Generation();
    click(40,148);click(130,148);click(225,148);click(320,148);click(415,148);click(40,148);
    click(420,198); /* change year, but do not apply */
    printf("EDITOR full_page_repaints=%u disk_reads=%u mounts=%lu\n",test_lcd_full_history_clears-clears,test_disk_reads-reads,(unsigned long)(HistoryFS_Generation()-generation));
    CHECK(test_lcd_full_history_clears==clears,"year/month/day editor updates only affected controls");
    CHECK(test_disk_reads==reads&&HistoryFS_Generation()==generation,"editing dates alone must not read SD");
    click(60,250); /* cancel editor */
    HistoryUI_Leave();TestDisk_SetImage(argv[2]);HistoryUI_Init();HistoryUI_Enter();
    HistoryUI_Process(now+=300);click(405,52);reads=test_disk_reads;
    for(unsigned i=0;i<20;i++)HistoryUI_Process(now+=300);
    CHECK(test_disk_reads==reads&&History_Get()->status==HISTORY_IDLE,"cancel stops a pending reader");
    HistoryUI_Leave();CHECK(!test_disk_writes,"history remains read-only");
    printf("History flicker/sparse-query regression failures: %u\n",failures);return failures?1:0;
}
