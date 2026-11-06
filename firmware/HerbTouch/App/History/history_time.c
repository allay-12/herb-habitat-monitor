#include "history.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

uint8_t HistoryDate_Days(uint16_t y,uint8_t m)
{
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m<1 || m>12)return 0;
    return (uint8_t)(days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0)));
}
bool HistoryDate_ToSeconds(const HistoryDate *d,uint32_t *out)
{
    uint32_t days=0;
    if(!d || !out || d->year<2000 || d->year>2099 || d->month<1 || d->month>12 ||
       d->day<1 || d->day>HistoryDate_Days(d->year,d->month) || d->hour>23 || d->minute>59 || d->second>59)return false;
    for(uint16_t y=2000;y<d->year;y++)days+=365+(HistoryDate_Days(y,2)==29);
    for(uint8_t m=1;m<d->month;m++)days+=HistoryDate_Days(d->year,m);
    *out=((days+d->day-1)*24+d->hour)*3600+d->minute*60+d->second;
    return true;
}
void HistoryDate_FromSeconds(uint32_t t,HistoryDate *d)
{
    uint32_t days=t/86400,remain=t%86400;
    d->year=2000;
    while(days>=(uint32_t)(365+(HistoryDate_Days(d->year,2)==29)))
    {days-=365+(HistoryDate_Days(d->year,2)==29);d->year++;}
    d->month=1;
    while(days>=HistoryDate_Days(d->year,d->month))
    {days-=HistoryDate_Days(d->year,d->month);d->month++;}
    d->day=(uint8_t)(days+1);d->hour=(uint8_t)(remain/3600);
    d->minute=(uint8_t)((remain%3600)/60);d->second=(uint8_t)(remain%60);
}
bool HistoryDate_Parse(const char *s,uint32_t *out)
{
    HistoryDate d;
    unsigned y,m,day,h,min,sec;
    if(!s || strlen(s)!=19 || (s[10]!=' ' && s[10]!='T'))return false;
    for(unsigned i=0;i<19;i++) {
        if(i==4 || i==7) {if(s[i]!='-')return false;}
        else if(i==13 || i==16) {if(s[i]!=':')return false;}
        else if(i!=10 && (s[i]<'0' || s[i]>'9'))return false;
    }
    if(sscanf(s,"%4u-%2u-%2u%*c%2u:%2u:%2u",&y,&m,&day,&h,&min,&sec)!=6)return false;
    if(y>2099 || m>12 || day>31 || h>23 || min>59 || sec>59)return false;
    d=(HistoryDate){(uint16_t)y,(uint8_t)m,(uint8_t)day,(uint8_t)h,(uint8_t)min,(uint8_t)sec};
    return HistoryDate_ToSeconds(&d,out);
}
uint32_t HistoryDate_Floor(uint32_t t,HistoryUnit unit)
{
    HistoryDate d;
    if(unit==HISTORY_HOUR)return t-t%3600;
    if(unit==HISTORY_DAY)return t-t%86400;
    HistoryDate_FromSeconds(t,&d);d.day=1;d.hour=d.minute=d.second=0;
    HistoryDate_ToSeconds(&d,&t);return t;
}
bool HistoryDate_Shift(uint32_t t,HistoryUnit unit,int32_t amount,uint32_t *out)
{
    HistoryDate d;
    if(unit==HISTORY_MONTH) {
        int64_t months;
        HistoryDate_FromSeconds(t,&d);months=(int64_t)(d.year-2000)*12+d.month-1+amount;
        if(months<0 || months>=1200)return false;
        d.year=(uint16_t)(2000+months/12);d.month=(uint8_t)(months%12+1);
        if(d.day>HistoryDate_Days(d.year,d.month))d.day=HistoryDate_Days(d.year,d.month);
        return HistoryDate_ToSeconds(&d,out);
    } else {
        int64_t n=(int64_t)t+(int64_t)amount*(unit==HISTORY_HOUR?3600:86400);
        if(n<0 || n>3155759999LL)return false; /* 2099-12-31 23:59:59 */
        *out=(uint32_t)n;return true;
    }
}
void HistoryDate_Format(uint32_t t,char *s,uint32_t cap)
{
    HistoryDate d;HistoryDate_FromSeconds(t,&d);
    snprintf(s,cap,"%04u-%02u-%02u %02u:%02u",d.year,d.month,d.day,d.hour,d.minute);
}
