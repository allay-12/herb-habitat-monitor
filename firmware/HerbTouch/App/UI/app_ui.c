#include "app_ui.h"
#include "bsp_lcd.h"
#include "bsp_touch.h"
#include "app_model.h"
#include "ui_history.h"
#include "node_app.h"
#include "node_identity.h"
#include "node_settings.h"
#include "history_fs.h"
#include "diskio.h"
#include "sd_disk_status.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef enum {
    MODE_NORMAL = 0,
    MODE_CALIBRATE,
    MODE_TOUCH_TEST,
    MODE_DISPLAY_TEST,
    MODE_NODE_CONFIG
} UIMode_t;

static UIPage_t  s_current_page = UI_PAGE_OVERVIEW;
static UIMode_t  s_ui_mode      = MODE_NORMAL;

static uint8_t   s_brightness       = 50;
static uint8_t   s_timeout_idx      = 0;
static const uint16_t s_timeouts[]  = { 0, 30, 60, 120 };
static const char*    s_timeout_str[] = { "关闭", "30 秒", "60 秒", "120 秒" };

static bool      s_sleeping         = false;
static uint32_t  s_last_act_ms      = 0;

/* Calibration state */
static uint8_t   s_cal_step         = 0;
static uint16_t  s_cal_raw_x        = 0;
static uint16_t  s_cal_raw_y        = 0;
static uint8_t   s_cal_stable       = 0;
static bool      s_cal_can_cancel   = false;
static bool      s_cal_error        = false;
static uint32_t  s_cal_diag_ms      = 0;
static const uint16_t s_cal_targets[5][2] = {
    { 24, 24 }, { 455, 24 }, { 455, 295 }, { 24, 295 }, { 240, 160 }
};

/* Display test state */
static uint8_t   s_disp_test_idx    = 0;

/* Diagnostics last touch */
static int16_t   s_diag_last_x      = -1;
static int16_t   s_diag_last_y      = -1;
static int16_t   s_press_target    = -1;
static bool      s_cancel_click    = false;
static bool      s_swallow_touch   = false;
/* Ownership is chosen on DOWN and retained through release, even across the
   content/nav boundary. A drag must never become a click in the other view. */
static bool      s_history_gesture = false;
static unsigned s_node_choice,s_node_message;

/* Forward declarations */
static void UI_DrawPage(UIPage_t page);
static void UI_DrawStatusBar(void);
static void UI_DrawNavBar(void);
static void UI_DrawOverview(void);
static void UI_DrawSoil(void);
static void UI_DrawSettings(void);
static void UI_DrawDiagnostics(void);
static void UI_DrawCalibrate(void);
static void UI_DrawTouchTest(int16_t x, int16_t y);
static void UI_DrawDisplayTest(uint8_t idx);
static void UI_BeginCalibrate(void);
static int16_t UI_HitTarget(int16_t x, int16_t y);
static void UI_DrawNodeConfig(void);
static void UI_NodeConfigClick(int16_t x,int16_t y);

void UI_Init(void)
{
    LCD_Init();
    Touch_Init();
    Model_Init();
    HistoryUI_Init();

    s_current_page = UI_PAGE_OVERVIEW;
    s_ui_mode      = MODE_NORMAL;
    s_brightness   = LCD_GetBacklight();
    s_last_act_ms  = HAL_GetTick();
    s_press_target = -1;s_cancel_click=false;s_history_gesture=false;s_swallow_touch=false;

    /* A guessed raw-to-pixel transform cannot reliably reach a calibration
       button. With RAM-only calibration, calibrate at every cold start. */
    if (Touch_GetCalStatus() != TOUCH_CAL_READY)
    {
        UI_BeginCalibrate();
    }
    else
    {
        UI_DrawPage(s_current_page);
    }
}

static void UI_DrawStatusBar(void)
{
    LCD_Fill(0, 0, 480, 28, LCD_COLOR_BAR_GRAY);
    LCD_DrawHLine(0, 27, 480, LCD_COLOR_BORDER_GRAY);
    char title[64];snprintf(title,sizeof(title),"药材生境 · %s",Model_GetSnapshot()->node_id);
    LCD_ShowStringUTF8(12, 6, title, LCD_COLOR_DARK_TEXT, LCD_COLOR_BAR_GRAY);
    LCD_ShowStringUTF8(354, 6, Model_IsDisplayTest()?"测试数据":"触摸联调", LCD_COLOR_DARK_TEXT, LCD_COLOR_BAR_GRAY);
}

static void UI_DrawNavBar(void)
{
    const char *tabs[] = { "概览", "土壤", "历史", "设置" };
    for (uint8_t i = 0; i < 4; i++)
    {
        uint16_t x = i * 120;
        uint16_t y = 276;
        uint16_t bg = (i == s_current_page) ? LCD_COLOR_DARK_TEXT : LCD_COLOR_WHITE;
        uint16_t fg = (i == s_current_page) ? LCD_COLOR_WHITE : LCD_COLOR_DARK_TEXT;

        LCD_Fill(x, y, 120, 44, bg);
        LCD_DrawRect(x, y, 120, 44, LCD_COLOR_BORDER_GRAY);
        LCD_ShowStringUTF8(x + 44, y + 14, tabs[i], fg, bg);
    }
}

static void UI_RenderOverview(bool full)
{
    const HerbDataModel_t *model=Model_GetSnapshot();
    char value[64];
    if(full)LCD_Fill(0, 28, 480, 248, LCD_COLOR_WHITE);

    if(full)LCD_ShowStringUTF8(12, 40, "环境数据", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    snprintf(value,sizeof(value),"更新：%-16s",model->update_time);
    LCD_ShowStringUTF8(300, 44, value, LCD_COLOR_TEXT_MUTED, LCD_COLOR_WHITE);

    /* 3 Cards: Air Temp, Air Humi, Light */
    const char *titles[] = { "空气温度", "空气湿度", "光照" };
    const char *units[]  = { "℃", "%", "lx" };
    for (uint8_t i = 0; i < 3; i++)
    {
        uint16_t x = 12 + i * 156;
        uint16_t y = 76;
        if(full) {
            LCD_Fill(x, y, 144, 84, LCD_COLOR_WHITE);
            LCD_DrawRect(x, y, 144, 84, LCD_COLOR_BORDER_GRAY);
            LCD_ShowStringUTF8(x + 10, y + 8, titles[i], LCD_COLOR_TEXT_DIM, LCD_COLOR_WHITE);
        }
        strcpy(value,"--");
        if(i==0 && model->air_temp_valid)snprintf(value,sizeof(value),"%.1f",model->air_temperature_c);
        if(i==1 && model->air_humi_valid)snprintf(value,sizeof(value),"%.1f",model->air_humidity_pct);
        if(i==2 && model->light_valid)snprintf(value,sizeof(value),"%lu",(unsigned long)model->illuminance_lux);
        char padded[16];snprintf(padded,sizeof(padded),"%-6s",value);
        LCD_ShowString(x + 12, y + 36, padded, 24, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
        if(full)LCD_ShowStringUTF8(x + 94, y + 42, units[i], LCD_COLOR_TEXT_DIM, LCD_COLOR_WHITE);
    }

    /* Battery Box */
    if(full) {
        LCD_Fill(12, 172, 456, 44, LCD_COLOR_BG_GRAY);
        LCD_DrawRect(12, 172, 456, 44, LCD_COLOR_BORDER_GRAY);
        LCD_ShowStringUTF8(24, 186, "电池：-- V     电量：未知", LCD_COLOR_DARK_TEXT, LCD_COLOR_BG_GRAY);
    }

    /* Status Footnotes */
    snprintf(value,sizeof(value),"空气：%s / 土壤：%s    ",model->air_status,model->soil_status);
    LCD_ShowStringUTF8(14, 229, value, LCD_COLOR_TEXT_DIM, LCD_COLOR_WHITE);
    if(full)LCD_ShowStringUTF8(14, 253, Model_IsDisplayTest()?"仅联调测试 · 非真实采样":"本机查看 · 点击数值查看历史", LCD_COLOR_TEXT_MUTED, LCD_COLOR_WHITE);
}
static void UI_DrawOverview(void){UI_RenderOverview(true);}

static void UI_RenderSoil(bool full)
{
    const HerbDataModel_t *model=Model_GetSnapshot();char value[24];
    if(full)LCD_Fill(0, 28, 480, 248, LCD_COLOR_WHITE);

    const char *labels[] = {
        "土壤温度", "土壤水分", "电导率", "酸碱度",
        "氮 N（参考）", "磷 P（参考）", "钾 K（参考）", "采集状态"
    };
    const char *units[] = {
        "℃", "%", "μS/cm", "pH",
        "mg/kg", "mg/kg", "mg/kg", ""
    };

    for (uint8_t i = 0; i < 8; i++)
    {
        uint16_t x = 12 + (i % 2) * 234;
        uint16_t y = 36 + (i / 2) * 59;
        if(full) {
            LCD_Fill(x, y, 222, 53, LCD_COLOR_WHITE);
            LCD_DrawRect(x, y, 222, 53, LCD_COLOR_BORDER_GRAY);
            LCD_ShowStringUTF8(x + 10, y + 6, labels[i], LCD_COLOR_TEXT_DIM, LCD_COLOR_WHITE);
        }
        if (i < 7)
        {
            strcpy(value,"--");
            if(i==0 && model->soil_temp_valid)snprintf(value,sizeof(value),"%.1f",model->soil_temperature_c);
            if(i==1 && model->soil_moist_valid)snprintf(value,sizeof(value),"%.1f",model->soil_moisture_pct);
            if(i==2 && model->soil_ec_valid)snprintf(value,sizeof(value),"%u",model->soil_ec_us_cm);
            if(i==3 && model->soil_ph_valid)snprintf(value,sizeof(value),"%.1f",model->soil_ph);
            if(i>=4 && model->soil_npk_valid)snprintf(value,sizeof(value),"%u",i==4?model->soil_n_mg_kg:i==5?model->soil_p_mg_kg:model->soil_k_mg_kg);
            char padded[24];snprintf(padded,sizeof(padded),"%-15s",value);
            LCD_ShowString(x + 12, y + 26, padded, 16, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
            if(full)LCD_ShowStringUTF8(x + 144, y + 28, units[i], LCD_COLOR_TEXT_DIM, LCD_COLOR_WHITE);
        }
        else
        {
            snprintf(value,sizeof(value),"%s    ",model->soil_status);
            LCD_ShowStringUTF8(x + 12, y + 26, value, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
        }
    }
}
static void UI_DrawSoil(void){UI_RenderSoil(true);}

static void UI_DrawSettings(void)
{
    LCD_Fill(0, 28, 480, 248, LCD_COLOR_WHITE);

    /* Brightness */
    LCD_ShowStringUTF8(12, 46, "背光亮度", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Minus Button */
    LCD_Fill(154, 34, 54, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(154, 34, 54, 44, LCD_COLOR_BORDER_GRAY);
    /* Draw the operators explicitly: SimSun's U+2212 bitmap is empty. */
    LCD_Fill(172, 55, 18, 2, LCD_COLOR_DARK_TEXT);

    /* Brightness Value */
    char val_str[16];
    snprintf(val_str, sizeof(val_str), "%d %%", s_brightness);
    LCD_Fill(210, 36, 90, 40, LCD_COLOR_WHITE);
    LCD_ShowString(230, 48, val_str, 16, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Plus Button */
    LCD_Fill(306, 34, 54, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(306, 34, 54, 44, LCD_COLOR_BORDER_GRAY);
    LCD_Fill(324, 55, 18, 2, LCD_COLOR_DARK_TEXT);
    LCD_Fill(332, 47, 2, 18, LCD_COLOR_DARK_TEXT);

    /* Screen Timeout */
    LCD_ShowStringUTF8(12, 106, "自动熄屏", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    LCD_Fill(154, 94, 206, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(154, 94, 206, 44, LCD_COLOR_BORDER_GRAY);
    char to_str[32];
    snprintf(to_str, sizeof(to_str), "%s · 点击切换", s_timeout_str[s_timeout_idx]);
    LCD_ShowStringUTF8(166, 108, to_str, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Calibrate Button */
    LCD_Fill(12, 152, 222, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(12, 152, 222, 44, LCD_COLOR_BORDER_GRAY);
    LCD_ShowStringUTF8(80, 166, "触摸校准", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Runtime node identity; no transport or sensor is started here. */
    LCD_Fill(246, 152, 222, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(246, 152, 222, 44, LCD_COLOR_BORDER_GRAY);
    LCD_ShowStringUTF8(322, 166, "节点配置", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Footnotes */
    LCD_ShowStringUTF8(14, 213, "亮度和校准仅在本次运行生效", LCD_COLOR_TEXT_MUTED, LCD_COLOR_WHITE);
    LCD_ShowStringUTF8(14, 240, "节点配置需联网确认", LCD_COLOR_TEXT_MUTED, LCD_COLOR_WHITE);
}

static const char *node_binding_text(void)
{
    if(!NodeIdentity_Saved())return "未保存";
    switch(NodeIdentity_Binding()) {
    case IDENTITY_BOUND:return "服务器已确认";
    case IDENTITY_NODE_USED:return "节点已被占用";
    case IDENTITY_DEVICE_USED:return "本机已绑定其他节点";
    case IDENTITY_DENIED:return "设备未获授权";
    default:return "待联网确认";
    }
}
static bool node_config_busy(void)
{
    unsigned state=g_node.config_result;
    return state==NODECFG_CONNECTING || state==NODECFG_CHECKING ||
           state==NODECFG_DRAINING || state==NODECFG_CONFIRMING || state==NODECFG_RETRY_SAVE;
}
static const char *node_config_message(unsigned state)
{
    switch(state) {
    case NODECFG_OK:return "当前配置已保存";
    case NODECFG_CONNECTING:return "正在连接服务器";
    case NODECFG_CHECKING:return "正在查询节点占用";
    case NODECFG_AVAILABLE:return "节点空闲，可确认切换";
    case NODECFG_OWNED:return "该节点已属本机";
    case NODECFG_OCCUPIED:return "节点已被其他设备占用";
    case NODECFG_DRAINING:return "正在上传原节点待传记录";
    case NODECFG_CONFIRMING:return "正在等待服务器确认";
    case NODECFG_CONFLICT:return "服务器配置冲突，请核对原节点";
    case NODECFG_DENIED:return "设备未获授权或凭据未配置";
    case NODECFG_TIMEOUT:return "连接或上传超时，请重试";
    case NODECFG_RETRY_SAVE:return "SD保存待完成，正在重试";
    case NODECFG_NO_CARD:return "请插入SD卡";
    case NODECFG_BUSY:return "设备忙，请稍后重试";
    case NODECFG_CORRUPT:return "请输入1到9999的节点号";
    case NODECFG_OTHER_DEVICE:return "SD配置属于其他设备";
    case NODECFG_IO:return "SD配置读取失败";
    default:return "输入节点号，可查询或确认切换";
    }
}
static void UI_DrawNodeConfig(void)
{
    LCD_Fill(0,28,480,248,LCD_COLOR_WHITE);
    char text[96];
    snprintf(text,sizeof(text),"当前：%s · %s",NodeIdentity_Saved()?NodeIdentity_Id():"未配置",node_binding_text());
    LCD_ShowStringUTF8(12,36,text,LCD_COLOR_DARK_TEXT,LCD_COLOR_WHITE);
    if(s_node_choice)snprintf(text,sizeof(text),"目标节点：%u",s_node_choice);
    else snprintf(text,sizeof(text),"目标节点：请输入1到9999");
    LCD_ShowStringUTF8(12,58,text,LCD_COLOR_DARK_TEXT,LCD_COLOR_WHITE);
    s_node_message=g_node.config_result;
    LCD_ShowStringUTF8(12,80,node_config_message(s_node_message),LCD_COLOR_TEXT_MUTED,LCD_COLOR_WHITE);

    const char *keys[]={"1","2","3","删除","4","5","6","清空","7","8","9","0"};
    uint16_t foreground=node_config_busy()?LCD_COLOR_TEXT_MUTED:LCD_COLOR_DARK_TEXT;
    for(unsigned i=0;i<12;i++) {
        uint16_t x=(uint16_t)(12+(i%4)*116),y=(uint16_t)(104+(i/4)*36);
        LCD_DrawRect(x,y,108,32,LCD_COLOR_BORDER_GRAY);
        LCD_ShowStringUTF8(x+36,y+8,keys[i],foreground,LCD_COLOR_WHITE);
    }
    const char *actions[]={"查询占用","确认切换","返回"};
    for(unsigned i=0;i<3;i++) {
        uint16_t x=(uint16_t)(12+i*156);
        LCD_DrawRect(x,220,144,44,LCD_COLOR_BORDER_GRAY);
        LCD_ShowStringUTF8(x+40,234,actions[i],i==2?LCD_COLOR_DARK_TEXT:foreground,LCD_COLOR_WHITE);
    }
}
static void UI_NodeConfigClick(int16_t x,int16_t y)
{
    if(y>=276){s_ui_mode=MODE_NORMAL;UI_DrawPage((UIPage_t)(x/120));return;}
    int target=UI_HitTarget(x,y);
    if(target==44){s_ui_mode=MODE_NORMAL;UI_DrawPage(UI_PAGE_SETTINGS);return;}
    if(node_config_busy())return;
    if(target>=30 && target<42) {
        const int digits[]={1,2,3,-1,4,5,6,-2,7,8,9,0};
        int digit=digits[target-30];
        if(digit==-1)s_node_choice/=10;
        else if(digit==-2)s_node_choice=0;
        else if(s_node_choice<=NODE_MAX_NUMBER/10)s_node_choice=s_node_choice*10+(unsigned)digit;
        g_node.config_result=NODECFG_MISSING;
    } else if(target==42 || target==43) {
        if(!s_node_choice)g_node.config_result=NODECFG_CORRUPT;
        else {
            g_node.config_node_request=s_node_choice;
            g_node.config_result=NODECFG_CONNECTING;
            g_node.command=target==42?6:7;
        }
    }
    UI_DrawNodeConfig();
}

static void UI_DrawDiagnostics(void)
{
    LCD_Fill(0, 28, 480, 248, LCD_COLOR_WHITE);

    LCD_ShowStringUTF8(12, 40, "ST7796S / XPT2046", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    char buf[48];
    uint16_t id = LCD_ReadID();
    const char *cal_str = (Touch_GetCalStatus() == TOUCH_CAL_READY) ? "已就绪" : "待校准";
    snprintf(buf, sizeof(buf), "LCD ID：0x%04X     校准：%s", id, cal_str);
    LCD_ShowStringUTF8(12, 73, buf, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    if (s_diag_last_x >= 0 && s_diag_last_y >= 0)
    {
        snprintf(buf, sizeof(buf), "触摸坐标：%d , %d", s_diag_last_x, s_diag_last_y);
    }
    else
    {
        snprintf(buf, sizeof(buf), "触摸坐标：-- , --");
    }
    LCD_ShowStringUTF8(12, 103, buf, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* These are snapshots, not connectivity probes: drawing must not start a
       modem, remount the card or add background SD reads. */
    const char *sd=SDDisk_LastError()?"SD：读写异常":
        ((disk_status(0)&STA_NOINIT)?"SD：未初始化 · 查看历史时检测":"SD：已初始化 · 本地历史已接入");
    const char *modem=!g_node.modem_enabled?"4G：未启动":
        (g_node.online?"4G：已连接":"4G：连接中");
    LCD_ShowStringUTF8(12, 130, sd, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    LCD_ShowStringUTF8(12, 154, modem, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    LCD_ShowStringUTF8(12, 178, "RS485：驱动已接入；INA219：预留", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Touch Test Button */
    LCD_Fill(12, 205, 222, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(12, 205, 222, 44, LCD_COLOR_BORDER_GRAY);
    LCD_ShowStringUTF8(88, 219, "触摸测试", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    /* Display Test Button */
    LCD_Fill(246, 205, 222, 44, LCD_COLOR_WHITE);
    LCD_DrawRect(246, 205, 222, 44, LCD_COLOR_BORDER_GRAY);
    LCD_ShowStringUTF8(322, 219, "显示测试", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
}

static void UI_DrawPage(UIPage_t page)
{
    s_press_target=-1;s_cancel_click=false;s_history_gesture=false;
    if(page!=UI_PAGE_HISTORY)HistoryUI_Leave();
    UI_DrawStatusBar();
    s_current_page = page;

    switch (page)
    {
        case UI_PAGE_OVERVIEW:
            UI_DrawOverview();
            break;
        case UI_PAGE_SOIL:
            UI_DrawSoil();
            break;
        case UI_PAGE_HISTORY:
            HistoryUI_Enter();
            break;
        case UI_PAGE_SETTINGS:
            UI_DrawSettings();
            break;
        case UI_PAGE_DIAGNOSTICS:
            UI_DrawDiagnostics();
            break;
        default:
            break;
    }

    UI_DrawNavBar();
}

static void UI_DrawCalibrate(void)
{
    LCD_Clear(LCD_COLOR_WHITE);

    char buf[32];
    snprintf(buf, sizeof(buf), "校准流程 %d / 5", s_cal_step + 1);
    LCD_ShowStringUTF8(90, 70, buf, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    LCD_ShowStringUTF8(90, 104, "按住十字至坐标稳定，再松手", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    if (s_cal_error)
        LCD_ShowStringUTF8(90, 128, "校准失败，请重新校准", LCD_COLOR_RED, LCD_COLOR_WHITE);

    /* Cancel Button */
    if (s_cal_can_cancel)
    {
        LCD_Fill(180, 224, 120, 44, LCD_COLOR_WHITE);
        LCD_DrawRect(180, 224, 120, 44, LCD_COLOR_BORDER_GRAY);
        LCD_ShowStringUTF8(222, 237, "返回", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    }
    else
    {
        LCD_ShowStringUTF8(90, 237, "首次校准完成后进入界面", LCD_COLOR_TEXT_MUTED, LCD_COLOR_WHITE);
    }

    /* Target Cross */
    uint16_t tx = s_cal_targets[s_cal_step][0];
    uint16_t ty = s_cal_targets[s_cal_step][1];
    LCD_DrawCross(tx, ty, 14, LCD_COLOR_RED);
}

static void UI_BeginCalibrate(void)
{
    s_cal_can_cancel = Touch_GetCalStatus() == TOUCH_CAL_READY;
    s_ui_mode = MODE_CALIBRATE;
    s_cal_step = 0;
    s_cal_stable = 0;
    s_cal_error = false;
    s_press_target = -1;
    Touch_StartCalibrate();
    UI_DrawCalibrate();
}

/* Give each interactive target an identity, so a drag cannot activate a
   different button on release. Disabled controls have no identity. */
static int16_t UI_HitTarget(int16_t x, int16_t y)
{
    if (x < 0 || x >= 480 || y < 0 || y >= 320) return -1;
    if (y >= 276) return x / 120;
    if(s_current_page==UI_PAGE_OVERVIEW && y>=76 && y<160 && x>=12 && x<468 && (x-12)%156<144)return 50+(x-12)/156;
    if(s_current_page==UI_PAGE_SOIL && x>=12 && x<468 && y>=36 && y<266 && (x-12)%234<222 && (y-36)%59<53) {
        int index=((y-36)/59)*2+(x-12)/234;
        if(index<7)return 53+index;
    }
    if(s_ui_mode==MODE_NODE_CONFIG) {
        if(y>=104 && y<212 && x>=12 && x<468 && (x-12)%116<108 && (y-104)%36<32)
            return 30+((y-104)/36)*4+(x-12)/116;
        if(y>=220 && y<264 && x>=12 && x<468 && (x-12)%156<144)
            return 42+(x-12)/156;
        return -1;
    }
    if (s_current_page == UI_PAGE_SETTINGS)
    {
        if (x >= 154 && x < 208 && y >= 34 && y < 78) return 10;
        if (x >= 306 && x < 360 && y >= 34 && y < 78) return 11;
        if (x >= 154 && x < 360 && y >= 94 && y < 138) return 12;
        if (x >= 12 && x < 234 && y >= 152 && y < 196) return 13;
        if (x >= 246 && x < 468 && y >= 152 && y < 196) return 14;
    }
    if (s_current_page == UI_PAGE_DIAGNOSTICS && y >= 205 && y < 249)
    {
        if (x >= 12 && x < 234) return 20;
        if (x >= 246 && x < 468) return 21;
    }
    return -1;
}

static void UI_DrawTouchTest(int16_t x, int16_t y)
{
    LCD_Clear(LCD_COLOR_WHITE);
    LCD_ShowStringUTF8(12, 12, "触摸测试 · 点击顶部返回诊断", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);

    if (x >= 0 && y >= 0)
    {
        LCD_DrawCross(x, y, 16, LCD_COLOR_RED);
        char buf[32];
        snprintf(buf, sizeof(buf), "X=%d  Y=%d", x, y);
        LCD_ShowString(12, 44, buf, 16, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    }
}

static void UI_DrawDisplayTest(uint8_t idx)
{
    switch (idx)
    {
        case 0: LCD_Clear(LCD_COLOR_RED);   break;
        case 1: LCD_Clear(LCD_COLOR_GREEN); break;
        case 2: LCD_Clear(LCD_COLOR_BLUE);  break;
        case 3: LCD_Clear(LCD_COLOR_WHITE); break;
        case 4: LCD_Clear(LCD_COLOR_BLACK); break;
        case 5:
            /* Checkerboard */
            for (uint16_t y = 0; y < 320; y += 20)
            {
                for (uint16_t x = 0; x < 480; x += 20)
                {
                    uint16_t c = (((x + y) / 20) % 2) ? LCD_COLOR_WHITE : LCD_COLOR_DARK_TEXT;
                    LCD_Fill(x, y, 20, 20, c);
                }
            }
            break;
        case 6:
            /* Border Test */
            LCD_Clear(LCD_COLOR_WHITE);
            LCD_DrawRect(0, 0, 480, 320, LCD_COLOR_DARK_TEXT);
            LCD_DrawRect(4, 4, 472, 312, LCD_COLOR_DARK_TEXT);
            LCD_ShowStringUTF8(12, 20, "480 × 320 边框测试", LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
            break;
        default:
            break;
    }
}

static void UI_HandleNormalClick(int16_t x, int16_t y)
{
    /* Bottom Navigation Bar (y >= 276) */
    if (y >= 276)
    {
        uint8_t tab = x / 120;
        if (tab < 4 && tab != s_current_page)
        {
            s_current_page = (UIPage_t)tab;
            UI_DrawPage(s_current_page);
        }
        return;
    }

    int target=UI_HitTarget(x,y);
    if(target>=50&&target<60) {
        HistoryUI_SelectMetric((uint8_t)(target-50));UI_DrawPage(UI_PAGE_HISTORY);return;
    }

    if (s_current_page == UI_PAGE_SETTINGS)
    {
        if(x>=246 && x<468 && y>=152 && y<196) {
            s_ui_mode=MODE_NODE_CONFIG;
            s_node_choice=node_config_busy()?g_node.config_node_request:0;
            UI_DrawNodeConfig();return;
        }
        /* Brightness - */
        if (x >= 154 && x < 208 && y >= 34 && y < 78)
        {
            if (s_brightness > 10)
            {
                s_brightness -= 10;
                LCD_SetBacklight(s_brightness);
                UI_DrawSettings();
            }
        }
        /* Brightness + */
        else if (x >= 306 && x < 360 && y >= 34 && y < 78)
        {
            if (s_brightness < 100)
            {
                s_brightness += 10;
                LCD_SetBacklight(s_brightness);
                UI_DrawSettings();
            }
        }
        /* Screen Timeout button */
        else if (x >= 154 && x < 360 && y >= 94 && y < 138)
        {
            s_timeout_idx = (s_timeout_idx + 1) % 4;
            UI_DrawSettings();
        }
        /* Calibrate button */
        else if (x >= 12 && x < 234 && y >= 152 && y < 196)
        {
            UI_BeginCalibrate();
        }
    }
    else if (s_current_page == UI_PAGE_DIAGNOSTICS)
    {
        /* Touch Test */
        if (x >= 12 && x < 234 && y >= 205 && y < 249)
        {
            s_ui_mode = MODE_TOUCH_TEST;
            UI_DrawTouchTest(-1, -1);
        }
        /* Display Test */
        else if (x >= 246 && x < 468 && y >= 205 && y < 249)
        {
            s_ui_mode = MODE_DISPLAY_TEST;
            s_disp_test_idx = 0;
            UI_DrawDisplayTest(0);
        }
    }
}

void UI_ShowDataPage(UIPage_t page)
{
    if(page>UI_PAGE_HISTORY)return;
    s_ui_mode=MODE_NORMAL;s_current_page=page;s_sleeping=false;
    s_last_act_ms=HAL_GetTick();s_timeout_idx=0;LCD_SetBacklight(s_brightness);
    s_press_target=-1;s_cancel_click=false;s_swallow_touch=false;s_history_gesture=false;
    UI_DrawPage(page);
}

void UI_Process(uint32_t now_ms)
{
    static uint32_t painted_revision,painted_at;
    static uint32_t identity_revision;
    static unsigned history_node=1;
    if(identity_revision!=NodeIdentity_Revision() && (s_ui_mode==MODE_NORMAL || s_ui_mode==MODE_NODE_CONFIG)) {
        identity_revision=NodeIdentity_Revision();Model_SetNode(NodeIdentity_Id());UI_DrawStatusBar();
        if(history_node!=NodeIdentity_Number()) {
            history_node=NodeIdentity_Number();
            HistoryFS_SetTestMode(false);
            HistoryUI_Init();
            if(s_ui_mode==MODE_NORMAL && s_current_page==UI_PAGE_HISTORY)HistoryUI_Enter();
        }
        if(s_ui_mode==MODE_NODE_CONFIG)UI_DrawNodeConfig();
    }
    if(s_ui_mode==MODE_NODE_CONFIG && s_node_message!=g_node.config_result)UI_DrawNodeConfig();
    uint32_t revision=Model_Revision();
    if(revision!=painted_revision && now_ms-painted_at>=1000 &&
       s_ui_mode==MODE_NORMAL && !s_sleeping && (s_current_page==UI_PAGE_OVERVIEW || s_current_page==UI_PAGE_SOIL)) {
        if(s_current_page==UI_PAGE_SOIL)UI_RenderSoil(false);else UI_RenderOverview(false);
        painted_revision=revision;painted_at=now_ms;
    }
    HistoryUI_Process(now_ms);
    /* Check sleep timeout */
    if (!s_sleeping && s_ui_mode == MODE_NORMAL && s_timeouts[s_timeout_idx] > 0)
    {
        if (now_ms - s_last_act_ms >= (uint32_t)s_timeouts[s_timeout_idx] * 1000)
        {
            s_sleeping = true;
            LCD_SetBacklight(0);
        }
    }

    TouchEvent_t evt = Touch_Scan(now_ms);

    /* Low-rate raw diagnostics distinguish an inactive PEN wire from invalid
       ADC samples without repainting the entire screen during sampling. */
    if (s_ui_mode == MODE_CALIBRATE && now_ms - s_cal_diag_ms >= 100)
    {
        char raw_text[40];
        s_cal_diag_ms = now_ms;
        if (s_cal_stable)
            snprintf(raw_text, sizeof(raw_text), "PEN=%u X=%u Y=%u OK=%u", !Touch_IsPressed(), s_cal_raw_x, s_cal_raw_y, s_cal_stable);
        else
            snprintf(raw_text, sizeof(raw_text), "PEN=%u X=---- Y=----", !Touch_IsPressed());
        LCD_Fill(90, 184, 350, 20, LCD_COLOR_WHITE);
        LCD_ShowString(90, 186, raw_text, 16, LCD_COLOR_DARK_TEXT, LCD_COLOR_WHITE);
    }

    if (evt.type != TOUCH_EVT_NONE)
    {
        s_last_act_ms = now_ms;

        /* If asleep, wake up on first touch and swallow the event */
        if (s_sleeping)
        {
            s_sleeping = false;
            LCD_SetBacklight(s_brightness);
            s_swallow_touch = evt.type != TOUCH_EVT_UP;
            s_press_target = -1;
            s_history_gesture = false;
            return;
        }

        if (s_swallow_touch)
        {
            if (evt.type == TOUCH_EVT_UP) s_swallow_touch = false;
            return;
        }

        /* Update Diagnostics touch coordinate */
        s_diag_last_x = evt.x;
        s_diag_last_y = evt.y;

        if (s_ui_mode == MODE_NORMAL || s_ui_mode==MODE_NODE_CONFIG)
        {
            if(evt.type==TOUCH_EVT_DOWN)
                s_history_gesture=s_ui_mode==MODE_NORMAL && s_current_page==UI_PAGE_HISTORY && evt.y<276;
            if(s_history_gesture)
            {
                HistoryUI_Touch(&evt);
                if(evt.type==TOUCH_EVT_UP)s_history_gesture=false;
                return;
            }
            if (evt.type == TOUCH_EVT_DOWN)
            {
                s_press_target = UI_HitTarget(evt.x, evt.y);
                s_cancel_click = false;
            }
            else if (evt.type == TOUCH_EVT_MOVE)
            {
                if (UI_HitTarget(evt.x, evt.y) != s_press_target) s_cancel_click = true;
            }
            else if (evt.type == TOUCH_EVT_UP)
            {
                int16_t target = UI_HitTarget(evt.x, evt.y);
                if (!s_cancel_click && target >= 0 && target == s_press_target)
                {
                    if(s_ui_mode==MODE_NODE_CONFIG)UI_NodeConfigClick(evt.x,evt.y);
                    else UI_HandleNormalClick(evt.x, evt.y);
                }
                s_press_target = -1;
            }
        }
        else if (s_ui_mode == MODE_TOUCH_TEST)
        {
            if (evt.type == TOUCH_EVT_DOWN || evt.type == TOUCH_EVT_MOVE)
            {
                if (evt.y < 35 && evt.x < 150)
                {
                    /* Exit touch test */
                    s_ui_mode = MODE_NORMAL;
                    UI_DrawPage(UI_PAGE_DIAGNOSTICS);
                }
                else
                {
                    UI_DrawTouchTest(evt.x, evt.y);
                }
            }
        }
        else if (s_ui_mode == MODE_DISPLAY_TEST)
        {
            if (evt.type == TOUCH_EVT_UP)
            {
                s_disp_test_idx++;
                if (s_disp_test_idx < 7)
                {
                    UI_DrawDisplayTest(s_disp_test_idx);
                }
                else
                {
                    s_ui_mode = MODE_NORMAL;
                    UI_DrawPage(UI_PAGE_DIAGNOSTICS);
                }
            }
        }
        else if (s_ui_mode == MODE_CALIBRATE)
        {
            if (evt.type == TOUCH_EVT_DOWN || evt.type == TOUCH_EVT_MOVE)
            {
                uint16_t raw_x, raw_y;
                if (evt.type == TOUCH_EVT_DOWN) s_cal_stable = 0;
                if (Touch_ReadRaw(&raw_x, &raw_y))
                {
                    if (s_cal_stable && abs((int)raw_x - s_cal_raw_x) <= 80 && abs((int)raw_y - s_cal_raw_y) <= 80)
                    {
                        s_cal_raw_x = ((uint32_t)s_cal_raw_x + raw_x) / 2;
                        s_cal_raw_y = ((uint32_t)s_cal_raw_y + raw_y) / 2;
                        if (s_cal_stable < 255) s_cal_stable++;
                    }
                    else
                    {
                        s_cal_raw_x = raw_x;
                        s_cal_raw_y = raw_y;
                        s_cal_stable = 1;
                    }
                }
                else s_cal_stable = 0;
            }
            else if (evt.type == TOUCH_EVT_UP)
            {
                /* Check Cancel button (180, 224, w=120, h=44) */
                if (s_cal_can_cancel && evt.x >= 180 && evt.x < 300 && evt.y >= 224 && evt.y < 268)
                {
                    Touch_CancelCalibrate();
                    s_ui_mode = MODE_NORMAL;
                    UI_DrawPage(UI_PAGE_SETTINGS);
                    return;
                }

                /* PEN is already high here: use samples cached while pressed. */
                if (s_cal_stable >= 2)
                {
                    Touch_RecordCalPoint(s_cal_step, s_cal_raw_x, s_cal_raw_y);
                    s_cal_stable = 0;
                    s_cal_step++;

                    if (s_cal_step < 5)
                    {
                        UI_DrawCalibrate();
                    }
                    else
                    {
                        /* Finished 5 points */
                        if (Touch_FinishCalibrate())
                        {
                            s_ui_mode = MODE_NORMAL;
                            UI_DrawPage(UI_PAGE_SETTINGS);
                        }
                        else
                        {
                            s_cal_step = 0;
                            s_cal_error = true;
                            Touch_StartCalibrate();
                            UI_DrawCalibrate();
                        }
                    }
                }
            }
        }
    }
}
