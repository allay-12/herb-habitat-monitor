#include "history.h"
#include "history_fs.h"
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>
#include <errno.h>

static HistorySeries series;
static char line[HISTORY_LINE_SIZE];
static uint32_t file_date,newest_date;
static bool file_open,header_pending,newest_found;
static uint32_t data_revision;
static enum {JOB_NONE,JOB_LIST,JOB_LATEST,JOB_QUERY} job;
static const char *names[HISTORY_METRICS]={"空气温度","空气湿度","光照","土壤温度","土壤水分","土壤电导率","土壤酸碱度","土壤氮（参考）","土壤磷（参考）","土壤钾（参考）"};
static const char *units[HISTORY_METRICS]={"℃","%","lx","℃","%","μS/cm","pH","mg/kg","mg/kg","mg/kg"};
const char *History_MetricName(uint8_t n){return n<HISTORY_METRICS?names[n]:"--";}
const char *History_MetricUnit(uint8_t n){return n<HISTORY_METRICS?units[n]:"";}
const HistorySeries *History_Get(void){return &series;}

bool History_ParseRow(char *s,uint32_t *stamp,float v[HISTORY_METRICS],uint16_t *valid)
{
    char *fields[HISTORY_METRICS+1],*p=s;
    unsigned count=1;
    fields[0]=s;*valid=0;
    while(*p) {
        if(*p==',') {if(count>=HISTORY_METRICS+1)return false;*p=0;fields[count++]=p+1;}
        p++;
    }
    if(count!=HISTORY_METRICS+1 || !HistoryDate_Parse(fields[0],stamp))return false;
    for(unsigned i=0;i<HISTORY_METRICS;i++) {
        char *end;double x;
        v[i]=0;
        if(!fields[i+1][0])continue;
        errno=0;x=strtod(fields[i+1],&end);
        if(end==fields[i+1] || *end || errno==ERANGE || !(x<=FLT_MAX && x>=-FLT_MAX))return false;
        /* Known dimensionless limits; negative temperatures are valid. */
        if((i==1 || i==4) && (x<0 || x>100))return false;
        if(i==6 && (x<0 || x>14))return false;
        if(i!=0 && i!=3 && x<0)return false;
        v[i]=(float)x;*valid|=(uint16_t)(1u<<i);
    }
    return true;
}
static void close_job(void)
{
    HistoryFS_Close();HistoryFS_ListEnd();file_open=false;job=JOB_NONE;
}
void History_Init(void){close_job();memset(&series,0,sizeof(series));data_revision=0;}
void History_NotifyWrite(uint32_t stamp)
{data_revision++;if(!series.latest_valid || stamp>series.latest){series.latest=stamp;series.latest_valid=true;}}
uint32_t History_DataRevision(void){return data_revision;}
void History_Cancel(void){close_job();if(series.status==HISTORY_READING || series.status==HISTORY_FINDING)series.status=HISTORY_IDLE;}
static void fail(HistoryFSResult r)
{
    close_job();series.status=r==HFS_NO_CARD?HISTORY_NO_CARD:HISTORY_IO_ERROR;
}
static uint32_t unit_count(uint32_t start,uint32_t end,HistoryUnit u)
{
    uint32_t n;
    if(u==HISTORY_MONTH) {
        HistoryDate a,b;HistoryDate_FromSeconds(start,&a);HistoryDate_FromSeconds(end-1,&b);
        n=(uint32_t)((b.year-a.year)*12+b.month-a.month+1);
    } else n=(end-1-start)/(u==HISTORY_HOUR?3600:86400)+1;
    return n;
}
bool History_Query(uint8_t metric,HistoryUnit unit,uint32_t start,uint32_t end)
{
    HistoryFSResult r;uint32_t cursor,next,n;
    if(metric>=HISTORY_METRICS || (unsigned)unit>HISTORY_MONTH || start>=end || end>3155759999u)return false;
    close_job();HistoryFS_SelectMetric(metric);series.metric=metric;series.unit=unit;series.start=start;series.end=end;
    series.rows=series.bad_rows=series.bad_files=series.files=0;series.points=0;
    memset(series.point,0,sizeof(series.point));
    cursor=HistoryDate_Floor(start,unit);n=unit_count(cursor,end,unit);
    series.stride=(n+HISTORY_MAX_POINTS-1)/HISTORY_MAX_POINTS;
    series.resolution_seconds=unit==HISTORY_HOUR?3600u*series.stride:unit==HISTORY_DAY?86400u*series.stride:0;
    /* A one-hour view must expose minute samples instead of one hourly mean.
       Longer queries retain the existing hour/day/calendar-month aggregation. */
    if(unit==HISTORY_HOUR && end-start<=3600u) {
        cursor=start-start%60u;series.stride=1;series.resolution_seconds=60;
    }
    series.boundary[0]=cursor;
    while(cursor<end && series.points<HISTORY_MAX_POINTS) {
        if(series.resolution_seconds)next=cursor+series.resolution_seconds;
        else if(!HistoryDate_Shift(cursor,unit,(int32_t)series.stride,&next))next=end;
        if(next>end)next=end;
        series.boundary[++series.points]=next;cursor=next;
    }
    r=HistoryFS_Mount();if(r!=HFS_OK){fail(r);return true;}
    r=HistoryFS_ListBegin();
    if(r==HFS_MISSING){series.status=HISTORY_NO_DATA;return true;}
    if(r!=HFS_OK){fail(r);return true;}
    series.status=HISTORY_READING;job=JOB_QUERY;
    return true;
}
void History_Default(uint8_t metric,HistoryUnit unit)
{
    uint32_t start,end;
    if(!series.latest_valid){History_Refresh(metric,unit);return;}
    if(unit==HISTORY_HOUR){end=series.latest-series.latest%60u+60u;start=end>=3600u?end-3600u:0;}
    else if(unit==HISTORY_DAY){end=series.latest-series.latest%60u+60u;start=end>=86400u?end-86400u:0;}
    else {end=HistoryDate_Floor(series.latest,HISTORY_DAY)+86400u;start=end>=30u*86400u?end-30u*86400u:0;}
    History_Query(metric,unit==HISTORY_DAY?HISTORY_HOUR:unit==HISTORY_MONTH?HISTORY_DAY:unit,start,end);
}
void History_Refresh(uint8_t metric,HistoryUnit unit)
{
    HistoryFSResult r;
    close_job();HistoryFS_SelectMetric(metric);series.metric=metric;series.unit=unit;series.status=HISTORY_FINDING;
    series.points=0;series.rows=series.bad_rows=series.bad_files=series.files=0;
    series.latest_valid=false;newest_found=false;newest_date=0;
    r=HistoryFS_Mount();if(r!=HFS_OK){fail(r);return;}
    r=HistoryFS_ListBegin();
    if(r==HFS_MISSING){series.status=HISTORY_NO_DATA;return;}
    if(r!=HFS_OK){fail(r);return;}job=JOB_LIST;
}
static bool header_ok(char *s)
{
    if((unsigned char)s[0]==0xef && (unsigned char)s[1]==0xbb && (unsigned char)s[2]==0xbf)s+=3;
    return strcmp(s,HISTORY_CSV_HEADER)==0;
}
static void consume(uint32_t t,float value)
{
    unsigned lo=0,hi=series.points;
    if(t<series.start || t>=series.end)return;
    while(lo+1<hi){unsigned m=(lo+hi)/2;if(t<series.boundary[m])hi=m;else lo=m;}
    HistoryPoint *p=&series.point[lo];
    if(!p->count){p->min=p->max=value;p->first_stamp=p->last_stamp=t;}
    else{if(value<p->min)p->min=value;if(value>p->max)p->max=value;if(t<p->first_stamp)p->first_stamp=t;if(t>p->last_stamp)p->last_stamp=t;}
    p->sum+=value;p->count++;series.rows++;
}
void History_Process(void)
{
    uint32_t started=HistoryFS_Tick();
    for(unsigned budget=0;budget<32 && job!=JOB_NONE;budget++) {
        HistoryFSResult r;
        if(budget && HistoryFS_Tick()-started>=8)break;
        if(job==JOB_LIST) {
            uint32_t date;
            r=HistoryFS_ListNext(&date);
            if(r==HFS_SKIP)continue;
            if(r==HFS_OK){if(!newest_found || date>newest_date){newest_date=date;newest_found=true;}continue;}
            HistoryFS_ListEnd();
            if(r!=HFS_END){fail(r);break;}
            if(!newest_found){series.status=HISTORY_NO_DATA;job=JOB_NONE;break;}
            file_date=newest_date;r=HistoryFS_OpenDate(file_date);
            if(r!=HFS_OK){fail(r);break;}
            file_open=true;header_pending=true;job=JOB_LATEST;
        } else if(job==JOB_QUERY && !file_open) {
            uint32_t date;
            r=HistoryFS_ListNext(&date);
            if(r==HFS_SKIP)continue;
            if(r==HFS_END) {
                HistoryFS_ListEnd();
                job=JOB_NONE;
                series.status=series.rows?HISTORY_READY:((series.bad_rows||series.bad_files)?HISTORY_FORMAT_ERROR:HISTORY_NO_DATA);
                break;
            }
            if(r!=HFS_OK){fail(r);break;}
            /* Directory order need not be chronological. Each existing date is
               read once; empty calendar days require no open/probe at all. */
            if(date>=series.end || date+86400u<=series.start)continue;
            file_date=date;
            r=HistoryFS_OpenDate(file_date);
            if(r==HFS_MISSING)continue;
            if(r!=HFS_OK){fail(r);break;}
            file_open=true;header_pending=true;series.files++;
        } else {
            r=HistoryFS_ReadLine(line,sizeof(line));
            if(r==HFS_DRAIN)continue;
            if(r==HFS_SKIP){series.bad_rows++;continue;}
            if(r==HFS_END) {
                HistoryFS_Close();file_open=false;
                if(job==JOB_LATEST) {
                    job=JOB_NONE;
                    if(series.latest_valid)History_Default(series.metric,series.unit);
                    else series.status=series.bad_rows||series.bad_files?HISTORY_FORMAT_ERROR:HISTORY_NO_DATA;
                }
                continue;
            }
            if(r!=HFS_OK){fail(r);break;}
            if(header_pending) {
                header_pending=false;
                if(!header_ok(line)) {
                    series.bad_files++;HistoryFS_Close();file_open=false;
                    if(job==JOB_LATEST){job=JOB_NONE;series.status=HISTORY_FORMAT_ERROR;}
                }
                continue;
            }
            if(!line[0])continue;
            uint32_t stamp;uint16_t valid;float values[HISTORY_METRICS];
            if(!History_ParseRow(line,&stamp,values,&valid) || HistoryDate_Floor(stamp,HISTORY_DAY)!=file_date) {
                series.bad_rows++;continue;
            }
            if(job==JOB_LATEST) {
                if(!series.latest_valid || stamp>series.latest){series.latest=stamp;series.latest_valid=true;}
            } else if(valid & (1u<<series.metric))consume(stamp,values[series.metric]);
        }
    }
}
