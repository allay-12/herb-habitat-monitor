#include "history.h"
#include "disk_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned failures;
#define CHECK(c,msg) do {if(!(c)){printf("FAIL: %s\n",msg);failures++;}} while(0)
uint32_t HAL_GetTick(void){return 0;}
static uint32_t stamp(const char *s){uint32_t t=0;CHECK(HistoryDate_Parse(s,&t),"fixture timestamp parses");return t;}
static void finish(void)
{
    unsigned ticks;
    for(ticks=0;ticks<20000;ticks++) {
        HistoryStatus s=History_Get()->status;
        if(s!=HISTORY_FINDING&&s!=HISTORY_READING)break;
        History_Process();
    }
    CHECK(ticks<20000,"cooperative query reaches terminal state");
}
int main(int argc,char **argv)
{
    CHECK(argc==6,"five fixture images supplied");if(argc!=6)return 2;
    uint32_t t;HistoryDate d={2024,2,29,12,1,2};
    CHECK(HistoryDate_ToSeconds(&d,&t),"leap day valid");HistoryDate back;HistoryDate_FromSeconds(t,&back);
    CHECK(back.year==2024&&back.month==2&&back.day==29&&back.second==2,"calendar round trip");
    CHECK(!HistoryDate_Parse("2023-02-29 12:00:00",&t),"non-leap day rejected");
    CHECK(!HistoryDate_Parse("2026-13-01 00:00:00",&t),"invalid month rejected");
    t=stamp("2024-01-31 12:00:00");CHECK(HistoryDate_Shift(t,HISTORY_MONTH,1,&t),"month shift succeeds");HistoryDate_FromSeconds(t,&back);
    CHECK(back.month==2&&back.day==29,"month shift clamps to leap-month end");
    History_Init();TestDisk_SetImage(argv[1]);History_Refresh(0,HISTORY_HOUR);finish();
    CHECK(History_Get()->status==HISTORY_READY && History_Get()->latest==stamp("2026-10-31 03:00:00"),"real FAT16 latest discovery ignores unrelated files and torn rows");
    CHECK(History_Query(0,HISTORY_HOUR,stamp("2026-10-31 00:00:00"),stamp("2026-10-31 03:00:00")),"hour query accepted");finish();
    const HistorySeries *s=History_Get();
    CHECK(s->status==HISTORY_READY&&s->points==3&&s->rows==4,"hour query end exclusive; malformed and torn records skipped");
    CHECK(s->point[0].count==2&&fabs(s->point[0].sum/2-22)<0.01&&s->point[0].min==20&&s->point[0].max==24,"hour mean/min/max accurate for unsorted input");
    CHECK(s->point[1].count==0&&s->point[2].count==2&&s->point[2].sum==24,"missing values remain gaps and valid rows after long record survive");
    CHECK(s->bad_rows==6,"one malformed record counted once even if it spans buffer chunks");
    History_Query(3,HISTORY_HOUR,stamp("2026-10-31 00:00:00"),stamp("2026-10-31 03:00:00"));finish();
    CHECK(History_Get()->rows==5&&History_Get()->point[1].count==1,"soil metric independent of missing air value");
    History_Query(0,HISTORY_DAY,stamp("2026-09-30 00:00:00"),stamp("2026-11-01 00:00:00"));finish();s=History_Get();
    CHECK(s->points==32&&s->rows==7&&s->point[30].count==1&&s->point[31].count==5,"daily groups cross months and preserve absent days");
    History_Query(0,HISTORY_MONTH,stamp("2026-09-01 00:00:00"),stamp("2026-11-01 00:00:00"));finish();s=History_Get();
    CHECK(s->points==2&&s->point[0].count==1&&s->point[1].count==6,"calendar month groups use actual month boundaries");
    History_Query(0,HISTORY_HOUR,stamp("2024-01-01 00:00:00"),stamp("2027-01-01 00:00:00"));finish();s=History_Get();
    CHECK(s->points<=HISTORY_MAX_POINTS&&s->stride>1&&s->rows==8,"multi-year hourly query bounded to 96 bins without losing samples");
    CHECK(!History_Query(0,HISTORY_DAY,20,10),"reversed range rejected");
    History_Query(0,HISTORY_DAY,stamp("2026-07-01 00:00:00"),stamp("2026-07-03 00:00:00"));finish();
    CHECK(History_Get()->status==HISTORY_NO_DATA,"empty selected period returns no data");
    History_Query(0,HISTORY_DAY,stamp("2024-01-01 00:00:00"),stamp("2027-01-01 00:00:00"));History_Cancel();unsigned reads=test_disk_reads;History_Process();
    CHECK(History_Get()->status==HISTORY_IDLE&&test_disk_reads==reads,"cancel stops file reads");
    TestDisk_SetImage(argv[2]);History_Refresh(0,HISTORY_HOUR);finish();CHECK(History_Get()->status==HISTORY_FORMAT_ERROR,"wrong CSV header reported");
    TestDisk_SetImage(argv[3]);History_Refresh(0,HISTORY_HOUR);finish();CHECK(History_Get()->status==HISTORY_NO_DATA,"card with no data files reported");
    TestDisk_SetImage("missing-card.img");History_Refresh(0,HISTORY_HOUR);CHECK(History_Get()->status==HISTORY_NO_CARD,"no-card path is nonfatal");
    TestDisk_SetImage(argv[1]);History_Query(0,HISTORY_DAY,stamp("2026-10-31 00:00:00"),stamp("2026-11-01 00:00:00"));test_disk_fail_reads=1;finish();
    CHECK(History_Get()->status==HISTORY_IO_ERROR||History_Get()->status==HISTORY_NO_CARD,"read failure terminates query without drawing partial success");
    TestDisk_SetImage(argv[4]);History_Refresh(0,HISTORY_HOUR);finish();
    CHECK(History_Get()->status==HISTORY_READY,"real FAT32 image can be mounted and queried");
    History_Query(0,HISTORY_HOUR,stamp("2026-10-31 00:00:00"),stamp("2026-10-31 03:00:00"));finish();
    CHECK(History_Get()->rows==4&&History_Get()->point[0].sum==44,"FAT32 and FAT16 return identical records");
    TestDisk_SetImage(argv[5]);
    History_Query(0,HISTORY_HOUR,stamp("2026-10-31 00:00:00"),stamp("2026-10-31 01:00:00"));finish();
    CHECK(History_Get()->rows==1&&History_Get()->point[0].sum==25,"air category preferred over legacy duplicate date");
    History_Query(2,HISTORY_HOUR,stamp("2026-10-31 00:00:00"),stamp("2026-10-31 01:00:00"));finish();
    CHECK(History_Get()->point[0].sum==12345,"light metric reads light category");
    History_Query(3,HISTORY_HOUR,stamp("2026-10-31 00:00:00"),stamp("2026-10-31 01:00:00"));finish();
    CHECK(History_Get()->point[0].sum==19,"soil metric reads soil category");
    CHECK(test_disk_writes==0,"all queries perform zero disk writes");
    printf("History/FatFs regression failures: %u\n",failures);return failures?1:0;
}
