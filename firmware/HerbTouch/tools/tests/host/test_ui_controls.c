/* Exercise real UI routing and bitmap rendering, not copies of hit-test logic. */
#include "bsp_lcd.h"
#include "node_app.h"
#include "history.h"
#include "disk_image.h"
#include "lcd_capture.h"
#include <stdio.h>
#include <string.h>
static char labels[4096];
static void capture_text(uint16_t x,uint16_t y,const char *s,uint16_t f,uint16_t b);
#define LCD_ShowStringUTF8 capture_text
#include "app_ui.c"
#undef LCD_ShowStringUTF8
volatile NodeReport g_node;
static unsigned failures;
static uint32_t tick;
static TouchEvent_t next_event;
static uint8_t backlight=50;
static TouchCalStatus_t calibration=TOUCH_CAL_READY;
#define CHECK(c,msg) do{if(!(c)){printf("FAIL: %s\n",msg);failures++;}}while(0)
static void capture_text(uint16_t x,uint16_t y,const char *s,uint16_t f,uint16_t b)
{
    size_t at=strlen(labels),n=strlen(s);
    if(at+n+2<sizeof(labels)){memcpy(labels+at,s,n);labels[at+n]='\n';labels[at+n+1]=0;}
    LCD_ShowStringUTF8(x,y,s,f,b);
}
uint32_t HAL_GetTick(void){return tick;}
void LCD_Init(void){}
uint16_t LCD_ReadID(void){return 0x7796;}
void LCD_SetBacklight(uint8_t value){backlight=value;}
uint8_t LCD_GetBacklight(void){return backlight;}
void LCD_DrawCross(uint16_t x,uint16_t y,uint16_t s,uint16_t c)
{LCD_DrawHLine(x-s,y,2*s+1,c);LCD_DrawVLine(x,y-s,2*s+1,c);}
void Touch_Init(void){}
bool Touch_IsPressed(void){return next_event.type!=TOUCH_EVT_UP;}
bool Touch_ReadRaw(uint16_t *x,uint16_t *y){*x=1000;*y=1200;return true;}
TouchEvent_t Touch_Scan(uint32_t now){(void)now;return next_event;}
TouchCalStatus_t Touch_GetCalStatus(void){return calibration;}
void Touch_StartCalibrate(void){calibration=TOUCH_CALIBRATING;}
void Touch_CancelCalibrate(void){calibration=TOUCH_CAL_READY;}
bool Touch_RecordCalPoint(uint8_t p,uint16_t x,uint16_t y){(void)p;(void)x;(void)y;return true;}
bool Touch_FinishCalibrate(void){calibration=TOUCH_CAL_READY;return true;}
static void event(TouchEventType_t type,int x,int y)
{next_event=(TouchEvent_t){type,x,y};tick+=10;UI_Process(tick);}
static void click(int x,int y){event(TOUCH_EVT_DOWN,x,y);event(TOUCH_EVT_UP,x,y);}
static void history_mode(unsigned mode)
{
    calibration=TOUCH_CAL_READY;UI_Init();s_timeout_idx=0;s_sleeping=false;
    UI_DrawPage(UI_PAGE_HISTORY);
    if(mode==1)click(100,52);else if(mode==2)click(270,52);
}
static unsigned ink(unsigned x,unsigned y,unsigned w,unsigned h)
{
    unsigned n=0;
    for(unsigned j=y;j<y+h;j++)for(unsigned i=x;i<x+w;i++)if(TestLCD_Pixel(i,j)==LCD_COLOR_DARK_TEXT)n++;
    return n;
}
int main(int argc,char **argv)
{
    if(argc!=4)return 2;
    TestDisk_SetImage(argv[1]);
    const UIPage_t destinations[]={UI_PAGE_OVERVIEW,UI_PAGE_SOIL,UI_PAGE_SETTINGS};
    for(unsigned mode=0;mode<3;mode++)for(unsigned tab=0;tab<3;tab++){
        history_mode(mode);int x=destinations[tab]*120+60;
        click(x,298);
        CHECK(s_current_page==destinations[tab],"bottom tabs work in graph, metric picker and date editor");
        CHECK(History_Get()->status!=HISTORY_READING&&History_Get()->status!=HISTORY_FINDING,"leaving history cancels pending SD query");
        unsigned reads=test_disk_reads;
        for(unsigned i=0;i<8;i++)event(TOUCH_EVT_NONE,0,0);
        CHECK(test_disk_reads==reads,"history performs no background SD reads after exit");
        click(240,298);click(48,298);
        CHECK(s_current_page==UI_PAGE_OVERVIEW,"history can be reopened and exited after dismissing editor");
    }
    for(unsigned mode=0;mode<3;mode++){
        history_mode(mode);
        event(TOUCH_EVT_DOWN,336,298);event(TOUCH_EVT_MOVE,100,52);event(TOUCH_EVT_UP,336,298);
        CHECK(s_current_page==UI_PAGE_HISTORY,"nav-to-content drag never switches tabs");
        click(432,298);CHECK(s_current_page==UI_PAGE_SETTINGS,"next clean tap works after canceled drag");
        history_mode(mode);
        event(TOUCH_EVT_DOWN,100,52);event(TOUCH_EVT_MOVE,336,298);event(TOUCH_EVT_UP,336,298);
        CHECK(s_current_page==UI_PAGE_HISTORY,"content-to-nav drag never activates navigation");
        click(48,298);CHECK(s_current_page==UI_PAGE_OVERVIEW,"next clean tap works after content drag");
    }
    UI_DrawPage(UI_PAGE_SETTINGS);
    CHECK(ink(165,44,30,24)>=32,"minus button has visible ink inside its border");
    CHECK(ink(319,44,30,24)>=48,"plus button has visible ink inside its border");
    char path[1024];snprintf(path,sizeof(path),"%s/settings-controls.ppm",argv[2]);TestLCD_Save(path);
    s_brightness=50;click(180,55);CHECK(s_brightness==40&&backlight==40,"minus tap changes backlight PWM by 10 percent");
    click(332,55);CHECK(s_brightness==50&&backlight==50,"plus tap changes backlight PWM by 10 percent");
    for(unsigned i=0;i<15;i++)click(180,55);
    CHECK(s_brightness==10&&backlight==10,"minus clamps safely at 10 percent");
    for(unsigned i=0;i<15;i++)click(332,55);
    CHECK(s_brightness==100&&backlight==100,"plus clamps at 100 percent");
    labels[0]=0;UI_DrawPage(UI_PAGE_DIAGNOSTICS);
    CHECK(!strstr(labels,"预留：4G")&&!strstr(labels,"INA219 / SD"),"diagnostics no longer labels implemented 4G and SD as reserved");
    CHECK(strstr(labels,"4G：未启动")&&strstr(labels,"SD："),"diagnostics reports stopped modem separately from SD");
    snprintf(path,sizeof(path),"%s/diagnostics-controls.ppm",argv[2]);TestLCD_Save(path);
    g_node.modem_enabled=1;labels[0]=0;UI_DrawDiagnostics();
    CHECK(strstr(labels,"4G：连接中"),"enabled modem is not falsely reported online");
    g_node.online=1;labels[0]=0;UI_DrawDiagnostics();
    CHECK(strstr(labels,"4G：已连接"),"only actual online flag reports connected");
    g_node.online=g_node.modem_enabled=0;test_disk_fail_reads=1;labels[0]=0;UI_DrawDiagnostics();
    CHECK(strstr(labels,"SD：读写异常"),"SD I/O failure is visible without retrying reads");
    CHECK(!test_lcd_missing_glyphs&&!test_lcd_out_of_bounds,"new controls and status labels render within screen with complete fonts");
    CHECK(test_disk_writes==0,"navigation/settings/diagnostics never write SD card");
    test_disk_fail_reads=0;TestDisk_SetImage(argv[3]);TestDisk_AllowWrites(1);
    NodeIdentity_Init(0x11111111,0x22222222,0x33333333);History_Init();NodeSettings_Load();
    s_ui_mode=MODE_NORMAL;UI_DrawPage(UI_PAGE_SETTINGS);click(350,174);
    CHECK(s_ui_mode==MODE_NODE_CONFIG,"settings opens manual node editor on release");
    click(65,119);click(410,191);click(410,191);click(65,119);
    CHECK(s_node_choice==1001&&!NodeIdentity_Saved(),"keypad accepts a four-digit node number");
    click(65,243);
    CHECK(g_node.command==6&&g_node.config_node_request==1001&&!NodeIdentity_Saved(),"query uses online command without changing SD identity");
    g_node.command=0;g_node.config_result=NODECFG_OCCUPIED;
    event(TOUCH_EVT_NONE,0,0);CHECK(strstr(labels,"节点已被其他设备占用"),"server conflict appears on the settings screen");
    g_node.sensor_enabled=1;
    click(220,243);
    CHECK(g_node.command==7&&g_node.config_node_request==1001&&!NodeIdentity_Saved(),"confirmation works while acquisition is enabled");
    click(60,298);CHECK(s_ui_mode==MODE_NORMAL&&s_current_page==UI_PAGE_OVERVIEW,"navigation works while server confirmation is pending");
    g_node.command=0;g_node.sensor_enabled=0;
    CHECK(!test_lcd_missing_glyphs&&!test_lcd_out_of_bounds,"node editor fits the framebuffer with all Chinese glyphs");
    UI_ShowDataPage(UI_PAGE_OVERVIEW);
    unsigned live_clears=test_lcd_full_history_clears;
    Model_SetAir(true,22.3f,65.2f,113,tick);tick+=1200;event(TOUCH_EVT_NONE,0,0);
    CHECK(test_lcd_full_history_clears==live_clears,"new air reading must not clear the overview content area");
    UI_ShowDataPage(UI_PAGE_SOIL);live_clears=test_lcd_full_history_clears;
    Model_SetSoil(true,19.4f,13,0,5.3f,0,0,0,tick);tick+=1200;event(TOUCH_EVT_NONE,0,0);
    CHECK(test_lcd_full_history_clears==live_clears,"new soil reading must not clear the soil content area");
    for(unsigned i=0;i<10;i++) {
        if(i==0){TestDisk_SetImage(argv[1]);NodeIdentity_Select(1,true);HistoryUI_Init();}
        UI_ShowDataPage(i<3?UI_PAGE_OVERVIEW:UI_PAGE_SOIL);
        int index=i<3?i:i-3;
        click(i<3?84+index*156:120+(index%2)*234,i<3?118:62+(index/2)*59);
        CHECK(s_current_page==UI_PAGE_HISTORY&&History_Get()->metric==i,"data card opens corresponding metric history");
        for(unsigned j=0;j<20000&&(History_Get()->status==HISTORY_FINDING||History_Get()->status==HISTORY_READING);j++){tick+=10;UI_Process(tick);}
        CHECK(History_Get()->end-History_Get()->start==3600,"card shortcut always opens recent one-hour history");
    }
    labels[0]=0;UI_DrawNavBar();CHECK(!strstr(labels,"诊断"),"diagnostics tab removed from visible navigation");
    printf("UI controls regression failures: %u\n",failures);return failures?1:0;
}
