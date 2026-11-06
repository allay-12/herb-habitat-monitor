#include "ui_history.h"
#include "history.h"
#include "node_config.h"
#include "bsp_lcd.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define PLOT_X 58
#define PLOT_Y 153
#define PLOT_W 408
#define PLOT_H 82
static uint8_t metric,mode,edit_end,edit_field;
static HistoryUnit unit,edit_unit;
static HistoryDate edit[2];
static int16_t pressed=-1,selected=-1;
static bool canceled,entered,started;
static uint32_t last_redraw;
static HistoryStatus last_status;
static uint32_t progress_files,progress_rows;
static bool progress_valid;
static bool follow_latest=true,automatic_refresh,plot_valid,marker_valid;
static uint32_t seen_data_revision,last_refresh,selection_time;
static double plot_lo,plot_hi;
static int marker_x,marker_y;
#define MASK_BYTES (((PLOT_W+7)/8)*(PLOT_H+1))
static uint8_t plot_mask[2][MASK_BYTES],mask_index;
static void plot_set(int x,int y)
{
    x-=PLOT_X;y-=PLOT_Y;
    if(x>=0&&x<PLOT_W&&y>=0&&y<=PLOT_H)plot_mask[mask_index^1u][y*((PLOT_W+7)/8)+x/8]|=(uint8_t)(1u<<(x&7));
}
static bool mask_pixel(unsigned index,int x,int y)
{return (plot_mask[index][y*((PLOT_W+7)/8)+x/8]&(1u<<(x&7)))!=0;}
static uint16_t background(int x,int y)
{return x==0||y==PLOT_H?LCD_COLOR_BORDER_GRAY:LCD_COLOR_WHITE;}
static void restore_marker(void)
{
    if(!marker_valid)return;
    for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++) {
        int x=marker_x+dx-PLOT_X,y=marker_y+dy-PLOT_Y;
        if(x>=0&&x<PLOT_W&&y>=0&&y<=PLOT_H)LCD_DrawPoint(x+PLOT_X,y+PLOT_Y,mask_pixel(mask_index,x,y)?LCD_COLOR_BLUE:background(x,y));
    }
    marker_valid=false;
}
static const char *unit_names[]={"小时","天","月"};
static void draw(void);

static void button(int x,int y,int w,const char *text,bool active)
{
    uint16_t bg=active?LCD_COLOR_DARK_TEXT:LCD_COLOR_WHITE,fg=active?LCD_COLOR_WHITE:LCD_COLOR_DARK_TEXT;
    LCD_Fill(x,y,w,40,bg);LCD_DrawRect(x,y,w,40,LCD_COLOR_BORDER_GRAY);
    LCD_ShowStringUTF8(x+8,y+12,text,fg,bg);
}
static const char *status_text(HistoryStatus s)
{
    switch(s) {
    case HISTORY_FINDING:return "正在查找本地记录，可取消";
    case HISTORY_READING:return "正在读取SD卡，可取消";
    case HISTORY_NO_CARD:return "未检测到SD卡，插卡后点刷新";
    case HISTORY_NO_DATA:return "所选时段没有该指标的数据";
    case HISTORY_FORMAT_ERROR:return "CSV格式错误，请检查数据文件";
    case HISTORY_IO_ERROR:return "读卡失败，请检查FAT32与连接";
    default:return "选择指标和时间查看本地记录";
    }
}
static int point_y(float v,double lo,double hi)
{
    double t=((double)v-lo)/(hi-lo);if(t<0)t=0;if(t>1)t=1;
    return PLOT_Y+PLOT_H-(int)(t*PLOT_H+0.5f);
}
static void axis_text(char *text,unsigned capacity,double value)
{
    if(fabs(value)>=10000.0 || (value!=0 && fabs(value)<0.1))snprintf(text,capacity,"%.0e",value);
    else snprintf(text,capacity,"%.1f",value);
}
static void line(int x0,int y0,int x1,int y1,uint16_t color)
{
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
    int dy=-(y1>y0?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){(void)color;plot_set(x0,y0);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
static bool reading(HistoryStatus status){return status==HISTORY_READING || status==HISTORY_FINDING;}
static void draw_progress(void)
{
    const HistorySeries *s=History_Get();char text[64];
    if(s->status!=HISTORY_READING)return;
    if(progress_valid && progress_files==s->files && progress_rows==s->rows)return;
    progress_valid=true;progress_files=s->files;progress_rows=s->rows;
    LCD_Fill(12,207,456,16,LCD_COLOR_WHITE);
    snprintf(text,sizeof(text),"%lu files / %lu samples",(unsigned long)s->files,(unsigned long)s->rows);
    LCD_ShowString(12,207,text,16,LCD_COLOR_TEXT_DIM,LCD_COLOR_WHITE);
}
static int point_x(const HistorySeries *s,unsigned index)
{
    const HistoryPoint *p=&s->point[index];
    uint32_t t=p->first_stamp+(p->last_stamp-p->first_stamp)/2;
    if(t<s->start)t=s->start;
    if(t>=s->end)t=s->end-1;
    return PLOT_X+(int)(((uint64_t)(t-s->start)*(PLOT_W-1))/(s->end-s->start));
}
static void draw_details(void)
{
    const HistorySeries *s=History_Get();char text[192];
    if(selected>=0&&selected<s->points&&s->point[selected].count) {
        const HistoryPoint *p=&s->point[selected];char when[24];HistoryDate first;
        HistoryDate_FromSeconds(p->first_stamp,&first);
        snprintf(when,sizeof(when),"%04u-%02u-%02u %02u:%02u:%02u",first.year,first.month,first.day,first.hour,first.minute,first.second);
        if(p->count==1)snprintf(text,sizeof(text),"时间：%s 数值：%.7g %s",when,p->sum/p->count,History_MetricUnit(metric));
        else {
            HistoryDate last;HistoryDate_FromSeconds(p->last_stamp,&last);
            snprintf(text,sizeof(text),"时间：%02u-%02u %02u:%02u~%02u-%02u %02u:%02u 均值：%.6g %s",first.month,first.day,first.hour,first.minute,last.month,last.day,last.hour,last.minute,p->sum/p->count,History_MetricUnit(metric));
        }
        marker_x=point_x(s,(unsigned)selected);marker_y=point_y((float)(p->sum/p->count),plot_lo,plot_hi);
        for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++)if(dx*dx+dy*dy<=9)
            if(marker_x+dx>=PLOT_X&&marker_x+dx<PLOT_X+PLOT_W&&marker_y+dy>=PLOT_Y&&marker_y+dy<=PLOT_Y+PLOT_H)
                LCD_DrawPoint(marker_x+dx,marker_y+dy,LCD_COLOR_RED);
        marker_valid=true;
    } else {
        uint32_t count=0;unsigned populated=0;
        for(unsigned i=0;i<s->points;i++){count+=s->point[i].count;if(s->point[i].count)populated++;}
        snprintf(text,sizeof(text),"共%lu条记录 / %u个点，点击查看时间和数值",(unsigned long)count,populated);
    }
    LCD_Fill(8,256,464,19,LCD_COLOR_WHITE);
    LCD_ShowStringUTF8(8,258,text,LCD_COLOR_DARK_TEXT,LCD_COLOR_WHITE);
}
static void draw_chart(void)
{
    const HistorySeries *s=History_Get();char text[96],label[32];
    progress_valid=false;
    if(s->status!=HISTORY_READY) {
        if(automatic_refresh&&reading(s->status))return;
        LCD_Fill(0,145,480,130,LCD_COLOR_WHITE);plot_valid=marker_valid=false;
        LCD_ShowStringUTF8(12,174,status_text(s->status),LCD_COLOR_TEXT_DIM,LCD_COLOR_WHITE);
        draw_progress();return;
    }
    float lo=0,hi=0;bool valid=false;
    for(unsigned i=0;i<s->points;i++)if(s->point[i].count) {
        const HistoryPoint *p=&s->point[i];
        if(!valid){lo=p->min;hi=p->max;valid=true;}else{if(p->min<lo)lo=p->min;if(p->max>hi)hi=p->max;}
    }
    if(!valid)return;
    restore_marker();
    if(!plot_valid) {
        LCD_Fill(0,145,480,130,LCD_COLOR_WHITE);memset(plot_mask,0,sizeof(plot_mask));
        LCD_DrawVLine(PLOT_X,PLOT_Y,PLOT_H+1,LCD_COLOR_BORDER_GRAY);
        LCD_DrawHLine(PLOT_X,PLOT_Y+PLOT_H,PLOT_W,LCD_COLOR_BORDER_GRAY);plot_valid=true;
    }
    plot_lo=lo;plot_hi=hi;
    if(plot_hi-plot_lo<0.001){double padding=fabs(plot_lo)*0.01;if(padding<1)padding=1;plot_lo-=padding;plot_hi+=padding;}
    memset(plot_mask[mask_index^1u],0,MASK_BYTES);
    int px=0,py=0;uint32_t previous_stamp=0;bool previous=false;
    bool minute_view=s->resolution_seconds==60;
    for(unsigned i=0;i<s->points;i++) {
        const HistoryPoint *p=&s->point[i];
        if(!p->count){if(!minute_view)previous=false;continue;}
        uint32_t stamp=p->first_stamp+(p->last_stamp-p->first_stamp)/2;
        int x=point_x(s,i),y=point_y((float)(p->sum/p->count),plot_lo,plot_hi);
        /* Empty minute buckets are expected between duty-cycle samples.
           Keep a real missing cycle visible, allowing a minute of timing jitter. */
        if(previous && (!minute_view || stamp-previous_stamp<=NODE_SENSOR_CYCLE_MS/1000u+60u))
            line(px,py,x,y,LCD_COLOR_BLUE);
        for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)plot_set(x+dx,y+dy);
        px=x;py=y;previous_stamp=stamp;previous=true;
    }
    /* A compact ink mask keeps automatic refresh from blanking the graph.
       Only pixels whose curve membership changed are written to the LCD. */
    for(int y=0;y<=PLOT_H;y++)for(int x=0;x<PLOT_W;x++) {
        bool old=mask_pixel(mask_index,x,y),fresh=mask_pixel(mask_index^1u,x,y);
        if(old!=fresh)LCD_DrawPoint(x+PLOT_X,y+PLOT_Y,fresh?LCD_COLOR_BLUE:background(x,y));
    }
    mask_index^=1u;
    axis_text(text,sizeof(text),plot_hi);snprintf(label,sizeof(label),"%-7.7s",text);LCD_ShowString(0,PLOT_Y,label,16,LCD_COLOR_TEXT_DIM,LCD_COLOR_WHITE);
    axis_text(text,sizeof(text),plot_lo);snprintf(label,sizeof(label),"%-7.7s",text);LCD_ShowString(0,PLOT_Y+PLOT_H-15,label,16,LCD_COLOR_TEXT_DIM,LCD_COLOR_WHITE);
    HistoryDate a,b;HistoryDate_FromSeconds(s->start,&a);HistoryDate_FromSeconds(s->end,&b);
    if(a.year!=b.year || unit==HISTORY_MONTH)snprintf(text,sizeof(text),"%02u-%02u-%02u   ",a.year%100,a.month,a.day);
    else snprintf(text,sizeof(text),"%02u-%02u %02u:%02u",a.month,a.day,a.hour,a.minute);
    LCD_ShowString(PLOT_X,238,text,16,LCD_COLOR_TEXT_DIM,LCD_COLOR_WHITE);
    if(a.year!=b.year || unit==HISTORY_MONTH)snprintf(text,sizeof(text),"%02u-%02u-%02u   ",b.year%100,b.month,b.day);
    else snprintf(text,sizeof(text),"%02u-%02u %02u:%02u",b.month,b.day,b.hour,b.minute);
    LCD_ShowString(PLOT_X+PLOT_W-88,238,text,16,LCD_COLOR_TEXT_DIM,LCD_COLOR_WHITE);
    if(selection_time && selected<0)for(unsigned i=0;i<s->points;i++)if(s->point[i].count&&s->point[i].first_stamp<=selection_time&&s->point[i].last_stamp>=selection_time){selected=(int16_t)i;break;}
    draw_details();
}
static void draw_editor_date(unsigned side)
{
    char text[64],when[24];uint32_t t;
    HistoryDate_ToSeconds(&edit[side],&t);HistoryDate_Format(t,when,sizeof(when));
    snprintf(text,sizeof(text),"%s %s",side?"止(不含)":"开始",when);button(8,32+side*46,464,text,edit_end==side);
}
static void draw_editor_field(unsigned field)
{
    static const char *fields[]={"年","月","日","时","分"};
    button(8+field*94,128,88,fields[field],edit_field==field);
}
static void draw_editor_value(void)
{
    char text[24];const HistoryDate *d=&edit[edit_end];
    unsigned value=edit_field==0?d->year:edit_field==1?d->month:edit_field==2?d->day:edit_field==3?d->hour:d->minute;
    LCD_Fill(118,178,250,40,LCD_COLOR_WHITE);
    snprintf(text,sizeof(text),"%u",value);LCD_ShowString(220,188,text,24,LCD_COLOR_DARK_TEXT,LCD_COLOR_WHITE);
}
static void draw_editor(void)
{
    char text[64];
    for(unsigned i=0;i<2;i++)draw_editor_date(i);
    for(unsigned i=0;i<5;i++)draw_editor_field(i);
    button(8,178,100,"减少",false);button(372,178,100,"增加",false);
    draw_editor_value();
    button(8,230,110,"取消",false);
    snprintf(text,sizeof(text),"粒度：%s",unit_names[edit_unit]);button(124,230,208,text,false);
    button(338,230,134,"应用",false);
}
static void draw_range(void)
{
    const HistorySeries *s=History_Get();char text[80];
    LCD_Fill(0,120,480,24,LCD_COLOR_WHITE);
    if(s->start<s->end) {
        char a[24],b[24];HistoryDate_Format(s->start,a,sizeof(a));HistoryDate_Format(s->end,b,sizeof(b));
        snprintf(text,sizeof(text),"%s - %s",a,b);LCD_ShowString(8,123,text,16,LCD_COLOR_DARK_TEXT,LCD_COLOR_WHITE);
        if(s->stride>1){snprintf(text,sizeof(text),"%lu%s/点",(unsigned long)s->stride,unit_names[unit]);LCD_ShowStringUTF8(364,123,text,LCD_COLOR_TEXT_MUTED,LCD_COLOR_WHITE);}
    }
}
static void draw(void)
{
    LCD_Fill(0,28,480,248,LCD_COLOR_WHITE);plot_valid=marker_valid=false;
    if(mode==1) {
        for(unsigned i=0;i<HISTORY_METRICS;i++)button(8+(i%2)*236,32+(i/2)*48,228,History_MetricName(i),metric==i);
        return;
    }
    if(mode==2){draw_editor();return;}
    char text[80];const HistorySeries *s=History_Get();
    snprintf(text,sizeof(text),"%s (%s)",History_MetricName(metric),History_MetricUnit(metric));
    button(8,32,210,text,false);button(224,32,112,"选时段",false);
    button(342,32,130,s->status==HISTORY_READING||s->status==HISTORY_FINDING?"取消读取":"刷新",false);
    for(unsigned i=0;i<3;i++)button(8+i*84,78,80,unit_names[i],unit==i);
    button(264,78,100,"上一段",false);button(370,78,102,"下一段",false);
    draw_range();
    draw_chart();
    last_status=s->status;last_redraw=HAL_GetTick();
}
void HistoryUI_Init(void)
{
    History_Init();metric=mode=edit_end=edit_field=0;unit=HISTORY_HOUR;entered=started=false;selected=pressed=-1;follow_latest=true;automatic_refresh=plot_valid=marker_valid=false;seen_data_revision=last_refresh=selection_time=0;
}
void HistoryUI_Enter(void)
{
    entered=true;mode=0;selected=pressed=-1;
    if(!started || History_Get()->status==HISTORY_IDLE || seen_data_revision!=History_DataRevision()){
        const HistorySeries *s=History_Get();
        if(!follow_latest&&s->start<s->end)History_Query(metric,unit,s->start,s->end);else History_Refresh(metric,unit);
        started=true;
    }
    seen_data_revision=History_DataRevision();
    last_status=History_Get()->status;draw();
}
void HistoryUI_SelectMetric(uint8_t requested)
{
    if(requested>=HISTORY_METRICS)return;
    metric=requested;unit=HISTORY_HOUR;follow_latest=true;automatic_refresh=false;
    started=false;selected=pressed=-1;selection_time=0;
}
void HistoryUI_Leave(void){entered=false;mode=0;pressed=selected=-1;canceled=false;History_Cancel();}
void HistoryUI_Process(uint32_t now)
{
    if(!entered)return;
    uint32_t revision=History_DataRevision();
    const HistorySeries *before=History_Get();
    if(mode==0&&!reading(before->status)&&revision!=seen_data_revision&&now-last_refresh>=1000) {
        seen_data_revision=revision;last_refresh=now;automatic_refresh=true;restore_marker();selected=-1;
        if(follow_latest)History_Default(metric,unit);
        else if(before->start<before->end)History_Query(metric,before->unit,before->start,before->end);
        /* A small daily file can complete in this very Process call. Track
           the new query state now so READY -> READY still paints new data. */
        last_status=History_Get()->status;
    }
    History_Process();
    HistoryStatus status=History_Get()->status;
    if(mode==0 && status!=last_status) {
        if(reading(status)!=reading(last_status))button(342,32,130,reading(status)?"取消读取":"刷新",false);
        draw_range();draw_chart();last_status=status;last_redraw=now;if(!reading(status))automatic_refresh=false;
    } else if(mode==0 && !automatic_refresh && reading(status) && now-last_redraw>=250) {
        last_redraw=now;draw_progress();
    }
}
static int hit(int x,int y)
{
    if(mode==1) {
        if(x<8 || x>=472 || y<32 || y>=272)return -1;
        unsigned col=x>=244,row=(y-32)/48;
        if((x-(col?244:8))>=228 || (y-32)%48>=40)return -1;
        return (int)(20+row*2+col);
    }
    if(mode==2) {
        if(x>=8 && x<472 && y>=32 && y<72)return 40;
        if(x>=8 && x<472 && y>=78 && y<118)return 41;
        if(y>=128 && y<168 && x>=8 && x<472 && (x-8)%94<88)return 42+(x-8)/94;
        if(y>=178 && y<218 && x>=8 && x<108)return 47;
        if(y>=178 && y<218 && x>=372 && x<472)return 48;
        if(y>=230 && y<270) {if(x>=8 && x<118)return 49;if(x>=124 && x<332)return 50;if(x>=338 && x<472)return 51;}
        return -1;
    }
    if(y>=32 && y<72){if(x>=8&&x<218)return 0;if(x>=224&&x<336)return 1;if(x>=342&&x<472)return 2;}
    if(y>=78&&y<118){if(x>=8&&x<256&&(x-8)%84<80)return 3+(x-8)/84;if(x>=264&&x<364)return 6;if(x>=370&&x<472)return 7;}
    if(x>=PLOT_X && x<PLOT_X+PLOT_W && y>=PLOT_Y && y<=PLOT_Y+PLOT_H)return 8;
    return -1;
}
static void adjust(int delta)
{
    HistoryDate *d=&edit[edit_end];int v;
    switch(edit_field) {
    case 0:v=d->year+delta;if(v>=2000&&v<=2099)d->year=(uint16_t)v;break;
    case 1:d->month=(uint8_t)((d->month-1+12+delta)%12+1);break;
    case 2:d->day=(uint8_t)((d->day-1+HistoryDate_Days(d->year,d->month)+delta)%HistoryDate_Days(d->year,d->month)+1);break;
    case 3:d->hour=(uint8_t)((d->hour+24+delta)%24);break;
    case 4:d->minute=(uint8_t)((d->minute+60+delta)%60);break;
    }
    if(d->day>HistoryDate_Days(d->year,d->month))d->day=HistoryDate_Days(d->year,d->month);
    draw_editor_date(edit_end);draw_editor_value();
}
static void select_point(int x)
{
    const HistorySeries *s=History_Get();if(s->status!=HISTORY_READY)return;
    restore_marker();selected=-1;int distance=PLOT_W+1;
    for(unsigned i=0;i<s->points;i++)if(s->point[i].count) {
        int dx=point_x(s,i)-x;if(dx<0)dx=-dx;
        if(dx<distance){distance=dx;selected=(int16_t)i;}
    }
    selection_time=selected>=0?s->point[selected].first_stamp:0;
    draw_details();
}
static void act(int target)
{
    const HistorySeries *s=History_Get();selected=-1;selection_time=0;
    if(mode==1) {
        if(target>=20&&target<30){metric=(uint8_t)(target-20);mode=0;if(follow_latest || s->start>=s->end)History_Refresh(metric,unit);else History_Query(metric,s->unit,s->start,s->end);draw();}
        return;
    }
    if(mode==2) {
        if(target==40||target==41) {
            uint8_t previous=edit_end;edit_end=(uint8_t)(target-40);
            if(previous!=edit_end){draw_editor_date(previous);draw_editor_date(edit_end);draw_editor_value();}
            return;
        }
        else if(target>=42&&target<=46) {
            uint8_t previous=edit_field;edit_field=(uint8_t)(target-42);
            if(previous!=edit_field){draw_editor_field(previous);draw_editor_field(edit_field);draw_editor_value();}
            return;
        }
        else if(target==47||target==48){adjust(target==47?-1:1);return;}
        else if(target==49)mode=0;
        else if(target==50) {
            char text[64];edit_unit=(HistoryUnit)((edit_unit+1)%3);
            snprintf(text,sizeof(text),"粒度：%s",unit_names[edit_unit]);button(124,230,208,text,false);return;
        }
        else if(target==51) {
            uint32_t a,b;
            if(!HistoryDate_ToSeconds(&edit[0],&a)||!HistoryDate_ToSeconds(&edit[1],&b)||a>=b) {
                LCD_Fill(112,183,256,32,LCD_COLOR_WHITE);
                LCD_ShowStringUTF8(120,190,"结束需晚于开始",LCD_COLOR_RED,LCD_COLOR_WHITE);return;
            }
            mode=0;unit=edit_unit;follow_latest=false;automatic_refresh=false;History_Query(metric,unit,a,b);
        }
        draw();return;
    }
    if(target==0){mode=1;draw();}
    else if(target==1) {
        uint32_t a=s->start,b=s->end;
        if(a>=b){HistoryDate d={2026,10,31,0,0,0};HistoryDate_ToSeconds(&d,&a);b=a+86400;}
        HistoryDate_FromSeconds(a,&edit[0]);HistoryDate_FromSeconds(b,&edit[1]);edit[0].second=edit[1].second=0;edit_end=edit_field=0;edit_unit=s->unit;mode=2;draw();
    } else if(target==2) {
        if(reading(s->status))History_Cancel();else if(follow_latest)History_Refresh(metric,unit);else History_Query(metric,s->unit,s->start,s->end);automatic_refresh=false;draw();
    } else if(target>=3&&target<=5) {
        HistoryUnit requested=(HistoryUnit)(target-3);
        if(requested==unit && reading(s->status))return;
        unit=requested;follow_latest=true;automatic_refresh=false;History_Default(metric,unit);draw();
    }
    else if(target==6||target==7) {
        if(s->start>=s->end)return;
        uint32_t a,b;int direction=target==6?-1:1;
        HistoryUnit query_unit=s->unit;
        if(query_unit==HISTORY_MONTH) {
            HistoryDate da,db;HistoryDate_FromSeconds(s->start,&da);HistoryDate_FromSeconds(s->end,&db);
            int n=(db.year-da.year)*12+db.month-da.month;if(n<1)n=1;
            if(!HistoryDate_Shift(s->start,unit,direction*n,&a)||!HistoryDate_Shift(s->end,unit,direction*n,&b))return;
        } else {
            int64_t span=(int64_t)s->end-s->start,na=(int64_t)s->start+direction*span,nb=(int64_t)s->end+direction*span;
            if(na<0||nb>3155759999LL)return;
            a=(uint32_t)na;b=(uint32_t)nb;
        }
        follow_latest=false;automatic_refresh=false;History_Query(metric,query_unit,a,b);draw();
    }
}
bool HistoryUI_Touch(const TouchEvent_t *e)
{
    if(e->y>=276) {
        /* Bottom navigation belongs to the app in every editor mode. Crossing
           into it cancels a content gesture; returning cannot reactivate it. */
        canceled=true;if(e->type==TOUCH_EVT_UP)pressed=-1;
        return false;
    }
    int target=hit(e->x,e->y);
    if(e->type==TOUCH_EVT_DOWN){pressed=(int16_t)target;canceled=false;if(target==8)select_point(e->x);}
    else if(e->type==TOUCH_EVT_MOVE){if(pressed==8&&target==8)select_point(e->x);else if(target!=pressed)canceled=true;}
    else if(e->type==TOUCH_EVT_UP){if(!canceled&&target>=0&&target==pressed&&target!=8)act(target);pressed=-1;}
    return true;
}
