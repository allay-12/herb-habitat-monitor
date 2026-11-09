/* Exercise production reader with unordered, sparse CSV files and boundary cases. */
#include "history.h"
#include "history_fs.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static uint32_t stamps[600],dates[400],cursor,opened,rows,days,tick,first;
static unsigned failures,reads;
static bool header;
static int missing=-1;
static bool no_card,broken_second_pass;
static unsigned passes;
#define CHECK(c,m) do{if(!(c)){printf("FAIL: %s\n",m);failures++;}}while(0)
void HistoryFS_SelectMetric(uint8_t m){(void)m;}
HistoryFSResult HistoryFS_Mount(void){passes=0;return no_card?HFS_NO_CARD:HFS_OK;}
HistoryFSResult HistoryFS_ListBegin(void){cursor=0;passes++;return broken_second_pass&&passes==2?HFS_ERROR:HFS_OK;}
HistoryFSResult HistoryFS_ListNext(uint32_t *d){if(cursor==days)return HFS_END;*d=dates[days-1-cursor++];return HFS_OK;}
void HistoryFS_ListEnd(void){}
HistoryFSResult HistoryFS_OpenDate(uint32_t d){opened=d;rows=0;header=true;return HFS_OK;}
void HistoryFS_Close(void){}
uint32_t HistoryFS_Tick(void){return tick++;}
HistoryFSResult HistoryFS_ReadLine(char *s,uint32_t cap)
{
    reads++;
    if(header){header=false;snprintf(s,cap,"%s",HISTORY_CSV_HEADER);return HFS_OK;}
    while(rows<first){unsigned index=first-1-rows++;uint32_t t=stamps[index];if(t/86400u*86400u==opened){
        HistoryDate d;HistoryDate_FromSeconds(t,&d);
        snprintf(s,cap,"%04u-%02u-%02u %02u:%02u:%02u,%s,50,100,10,40,50,7,1,2,3",d.year,d.month,d.day,d.hour,d.minute,d.second,(int)index==missing?"":"20");return HFS_OK;
    }}return HFS_END;
}
static uint32_t base;
static void data(unsigned n,uint32_t step)
{
    first=n;days=0;
    for(unsigned i=0;i<n;i++){
        stamps[i]=base+i*step;
        uint32_t day=stamps[i]/86400u*86400u;
        if(!days||dates[days-1]!=day)dates[days++]=day;
    }
}
static const HistorySeries *query(HistoryUnit view,uint32_t span)
{
    CHECK(History_QueryAdaptive(0,view,base,base+span),"adaptive query accepted");
    for(unsigned i=0;i<200000;i++){
        History_Process();if(History_Get()->status!=HISTORY_READING)break;
    }
    CHECK(History_Get()->status==HISTORY_READY,"adaptive query completes");return History_Get();
}
int main(void)
{
    HistoryDate_Parse("2026-01-01 00:00:00",&base);History_Init();
    data(20,180);const HistorySeries *s=query(HISTORY_HOUR,3600);
    CHECK(s->raw&&s->points==20&&s->rows==20,"hour retains each real record");
    CHECK(s->point[0].first_stamp==base&&s->point[19].first_stamp==base+3420,"raw records sorted even with reverse CSV and directory order");
    data(24,900);s=query(HISTORY_DAY,86400);
    CHECK(s->raw&&s->points==24,"24-hour view keeps 24 raw points");
    data(25,900);s=query(HISTORY_DAY,86400);
    CHECK(!s->raw&&s->unit==HISTORY_HOUR&&s->rows==25&&s->points==7,"25th record switches 24-hour view to exact hourly means");
    CHECK(s->point[0].count==4&&fabs(s->point[0].sum/4-20)<0.001,"aggregate preserves mean and count");
    missing=0;s=query(HISTORY_DAY,86400);
    CHECK(s->raw&&s->points==24&&s->plot_start==base+900,"missing metric values do not count toward 25 points or shift the valid origin");missing=-1;
    data(481,180);s=query(HISTORY_MONTH,30u*86400u);
    CHECK(s->raw&&s->points==481,"valid span of exactly 24h retains all raw records beyond old 96-point cap");
    data(2,86401);s=query(HISTORY_MONTH,30u*86400u);
    CHECK(!s->raw&&s->unit==HISTORY_HOUR&&s->points==2,"valid span over 24h switches to sparse hours without merging empty hours");
    data(72,3600);s=query(HISTORY_MONTH,30u*86400u);
    CHECK(!s->raw&&s->unit==HISTORY_HOUR&&s->points==72,"72 occupied hours stay hourly");
    data(73,3600);s=query(HISTORY_MONTH,30u*86400u);
    CHECK(s->unit==HISTORY_DAY&&s->points==4,"73 occupied hours switch to days");
    data(90,86400);s=query(HISTORY_YEAR,365u*86400u);
    CHECK(s->unit==HISTORY_DAY&&s->points==90,"90 occupied days stay daily");
    data(91,86400);s=query(HISTORY_YEAR,365u*86400u);
    CHECK(s->unit==HISTORY_MONTH&&s->points==4&&s->rows==91,"91 occupied days switch to calendar months");
    data(2,180u*86400u);s=query(HISTORY_YEAR,365u*86400u);
    CHECK(s->unit==HISTORY_HOUR&&s->points==2,"sparse year preserves occupied hours without stride compression");
    CHECK(s->plot_start==base,"plot origin tracks first valid actual sample");
    data(520,1);s=query(HISTORY_HOUR,3600);
    CHECK(!s->raw&&s->rows==520&&s->point[0].count==520,"abnormal raw overflow aggregates all records without truncation");
    History_QueryAdaptive(0,HISTORY_YEAR,base,base+365u*86400u);History_Process();History_Cancel();
    unsigned before=reads;History_Process();CHECK(reads==before&&History_Get()->status==HISTORY_IDLE,"cancel stops both adaptive phases");
    CHECK(!History_QueryAdaptive(0,HISTORY_YEAR,base,base+367u*86400u),"adaptive range bound protects occupancy masks");
    CHECK(!History_QueryAdaptive(0,HISTORY_DAY,base,base+30u*86400u),"preset span bound protects sparse aggregate capacity");
    data(0,1);History_QueryAdaptive(0,HISTORY_HOUR,base,base+3600);History_Process();
    CHECK(History_Get()->status==HISTORY_NO_DATA,"empty adaptive range is terminal");
    no_card=true;History_QueryAdaptive(0,HISTORY_HOUR,base,base+3600);
    CHECK(History_Get()->status==HISTORY_NO_CARD,"adaptive query preserves missing-card status");no_card=false;
    data(25,900);broken_second_pass=true;History_QueryAdaptive(0,HISTORY_DAY,base,base+86400);
    for(unsigned i=0;i<10000&&History_Get()->status==HISTORY_READING;i++)History_Process();
    CHECK(History_Get()->status==HISTORY_IO_ERROR,"failure between adaptive passes never presents a partial successful curve");
    printf("Adaptive history regression failures: %u\n",failures);return failures?1:0;
}
