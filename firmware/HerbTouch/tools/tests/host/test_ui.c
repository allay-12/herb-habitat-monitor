/* Runs the real UI state machine with hardware I/O replaced by deterministic input. */
#include <stdio.h>
#include <string.h>
#include "bsp_lcd_font.h"
#include "app_ui.c"
volatile NodeReport g_node;

static uint32_t tick;
static TouchEvent_t next_event;
static bool pen;
static TouchCalStatus_t cal_status;
static unsigned missing_glyphs, raw_reads_after_release, recorded;
static uint8_t backlight = 50;
static unsigned failures;
static bool finish_ok=true;
static bool saw_temperature,saw_humidity,saw_light;
static unsigned soil_values;
#define CHECK(c,msg) do { if (!(c)) {printf("FAIL: %s\n",msg);failures++;} } while(0)

uint32_t HAL_GetTick(void) { return tick; }
void LCD_Init(void) {}
void LCD_SetBacklight(uint8_t p) { backlight=p; }
uint8_t LCD_GetBacklight(void) { return backlight; }
void LCD_Clear(uint16_t c) {(void)c;}
void LCD_Fill(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t c) {(void)x;(void)y;(void)w;(void)h;(void)c;}
void LCD_DrawHLine(uint16_t x,uint16_t y,uint16_t w,uint16_t c) {(void)x;(void)y;(void)w;(void)c;}
void LCD_DrawVLine(uint16_t x,uint16_t y,uint16_t h,uint16_t c) {(void)x;(void)y;(void)h;(void)c;}
void LCD_DrawPoint(uint16_t x,uint16_t y,uint16_t c) {(void)x;(void)y;(void)c;}
void LCD_DrawRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t c) {(void)x;(void)y;(void)w;(void)h;(void)c;}
void LCD_DrawCross(uint16_t x,uint16_t y,uint16_t s,uint16_t c) {(void)x;(void)y;(void)s;(void)c;}
void LCD_ShowString(uint16_t x,uint16_t y,const char *s,uint8_t z,uint16_t f,uint16_t b) {
    (void)x;(void)y;(void)z;(void)f;(void)b;
    char unpadded[80];snprintf(unpadded,sizeof(unpadded),"%s",s);size_t length=strlen(unpadded);
    while(length&&unpadded[length-1]==' ')unpadded[--length]=0;
    s=unpadded;
    if(!strcmp(s,"-10.1"))saw_temperature=true;
    if(!strcmp(s,"65.8"))saw_humidity=true;
    if(!strcmp(s,"100000"))saw_light=true;
    const char *soil[]={"23.2","45.1","500","6.5","30","20","50"};
    for(unsigned i=0;i<7;i++)if(!strcmp(s,soil[i]))soil_values|=1u<<i;
}
void LCD_ShowStringUTF8(uint16_t x,uint16_t y,const char *s,uint16_t f,uint16_t b)
{
    const unsigned char *p=(const unsigned char *)s;
    (void)x;(void)y;(void)f;(void)b;
    while (*p) {
        uint32_t code;unsigned n;
        if (*p<128) {p++;continue;}
        if ((*p&0xe0)==0xc0) {code=*p&31;n=2;}
        else if ((*p&0xf0)==0xe0) {code=*p&15;n=3;}
        else {p++;continue;}
        for(unsigned i=1;i<n;i++)code=(code<<6)|(p[i]&63);
        if(!LCD_GetChineseFont16(code)) {printf("MISSING GLYPH U+%04X\n",(unsigned)code);missing_glyphs++;}
        p+=n;
    }
}
void Touch_Init(void) {cal_status=TOUCH_CAL_NONE;}
bool Touch_IsPressed(void) {return pen;}
bool Touch_ReadRaw(uint16_t *x,uint16_t *y) {
    if(!pen){raw_reads_after_release++;return false;}
    *x=1000;*y=1200;return true;
}
TouchEvent_t Touch_Scan(uint32_t now) {(void)now;return next_event;}
TouchCalStatus_t Touch_GetCalStatus(void) {return cal_status;}
void Touch_StartCalibrate(void) {cal_status=TOUCH_CALIBRATING;}
void Touch_CancelCalibrate(void) {cal_status=TOUCH_CAL_NONE;}
bool Touch_RecordCalPoint(uint8_t step,uint16_t x,uint16_t y) {(void)step;(void)x;(void)y;recorded++;return true;}
bool Touch_FinishCalibrate(void) {cal_status=finish_ok?TOUCH_CAL_READY:TOUCH_CAL_FAILED;return finish_ok;}
static void event(TouchEventType_t type,int16_t x,int16_t y) {
    next_event=(TouchEvent_t){type,x,y};pen=type!=TOUCH_EVT_UP;tick+=10;UI_Process(tick);
}
static void reset_ready(void) {
    UI_Init();s_ui_mode=MODE_NORMAL;s_current_page=UI_PAGE_OVERVIEW;
    cal_status=TOUCH_CAL_READY;s_sleeping=false;s_timeout_idx=0;
}
int main(void) {
    UI_Init();
    CHECK(s_ui_mode==MODE_CALIBRATE && s_cal_step==0,"cold start enters calibration without guessed-coordinate navigation");
    reset_ready();
    for(unsigned p=0;p<UI_PAGE_COUNT;p++)UI_DrawPage((UIPage_t)p);
    s_cal_step=0;UI_DrawCalibrate();
    CHECK(missing_glyphs==0,"real UI strings all have glyphs (including punctuation and micro sign)");
    Model_SetAir(true,-10.1f,65.8f,100000,1000);UI_DrawOverview();
    CHECK(saw_temperature && saw_humidity && saw_light,"real valid air readings render in overview");
    Model_SetAir(false,0,0,0,2000);
    CHECK(!Model_GetSnapshot()->air_temp_valid && !Model_GetSnapshot()->light_valid,"stale readings become invalid");
    soil_values=0;Model_SetSoil(true,23.2f,45.1f,500,6.5f,30,20,50,2100);UI_DrawSoil();
    CHECK(soil_values==127&&Model_GetSnapshot()->soil_temp_valid,"live soil readings render all seven fields without preview mode");
    Model_SetAir(false,0,0,0,2200);CHECK(Model_GetSnapshot()->soil_temp_valid,"air failure does not invalidate healthy soil");
    Model_SetSoil(false,0,0,0,0,0,0,0,2300);CHECK(!Model_GetSnapshot()->soil_temp_valid&&!Model_GetSnapshot()->soil_npk_valid,"soil timeout hides stale soil values");
    const float preview[]={25.6f,65.8f,12000,23.2f,45.1f,500,6.5f,30,20,50};
    CHECK(Model_SetDisplayTest(preview,"17:42:13"),"explicit preview model accepted");
    soil_values=0;UI_DrawOverview();UI_DrawSoil();
    CHECK(soil_values==127 && missing_glyphs==0,"all seven soil values and test labels render from the same model");
    Model_ClearDisplayTest();
    reset_ready();UI_HandleNormalClick(100,170); /* settings action is entered explicitly below */
    s_current_page=UI_PAGE_SETTINGS;UI_HandleNormalClick(100,170);
    recorded=0;raw_reads_after_release=0;
    event(TOUCH_EVT_DOWN,24,24);event(TOUCH_EVT_MOVE,24,24);event(TOUCH_EVT_UP,24,24);
    CHECK(recorded==1 && s_cal_step==1,"calibration stores pressed sample and advances once on release");
    CHECK(raw_reads_after_release==0,"calibration never rereads a released panel");
    UI_BeginCalibrate();recorded=0;
    event(TOUCH_EVT_DOWN,24,24);event(TOUCH_EVT_UP,24,24);
    CHECK(recorded==0 && s_cal_step==0,"unstable one-scan contact does not become a calibration point");
    for(unsigned i=0;i<5;i++) {
        event(TOUCH_EVT_DOWN,s_cal_targets[i][0],s_cal_targets[i][1]);
        for(unsigned hold=0;hold<5;hold++)event(TOUCH_EVT_MOVE,s_cal_targets[i][0],s_cal_targets[i][1]);
        CHECK(recorded==i,"long press never advances before release");
        event(TOUCH_EVT_UP,s_cal_targets[i][0],s_cal_targets[i][1]);
    }
    CHECK(recorded==5 && s_ui_mode==MODE_NORMAL && Touch_GetCalStatus()==TOUCH_CAL_READY,"all five press/release gestures complete calibration");
    finish_ok=false;UI_BeginCalibrate();
    for(unsigned i=0;i<5;i++) {
        event(TOUCH_EVT_DOWN,s_cal_targets[i][0],s_cal_targets[i][1]);
        event(TOUCH_EVT_MOVE,s_cal_targets[i][0],s_cal_targets[i][1]);
        event(TOUCH_EVT_UP,s_cal_targets[i][0],s_cal_targets[i][1]);
    }
    CHECK(s_ui_mode==MODE_CALIBRATE && s_cal_step==0 && s_cal_error,"failed five-point calibration remains visible and restarts at first target");
    finish_ok=true;
    reset_ready();event(TOUCH_EVT_DOWN,180,298);
    CHECK(s_current_page==UI_PAGE_OVERVIEW,"navigation waits for release");
    event(TOUCH_EVT_UP,180,298);
    CHECK(s_current_page==UI_PAGE_SOIL,"same-button release navigates once");
    reset_ready();event(TOUCH_EVT_DOWN,180,298);event(TOUCH_EVT_MOVE,300,298);event(TOUCH_EVT_UP,300,298);
    CHECK(s_current_page==UI_PAGE_OVERVIEW,"drag to another tab cancels click");
    reset_ready();s_sleeping=true;s_current_page=UI_PAGE_SETTINGS;
    event(TOUCH_EVT_DOWN,180,298);event(TOUCH_EVT_MOVE,180,298);event(TOUCH_EVT_UP,180,298);
    CHECK(!s_sleeping && s_current_page==UI_PAGE_SETTINGS,"entire wake gesture is swallowed");
    printf("UI regression failures: %u\n",failures);
    return failures?1:0;
}
