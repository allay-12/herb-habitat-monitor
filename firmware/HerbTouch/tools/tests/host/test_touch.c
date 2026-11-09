#include <stdio.h>
#include "bsp_touch.c"
static unsigned failures,pen_reads;
#define CHECK(c,msg) do {if(!(c)){printf("FAIL: %s\n",msg);failures++;}} while(0)
uint32_t HAL_GetTick(void) {return 0;}
void HAL_GPIO_WritePin(void *p,uint16_t n,GPIO_PinState s) {(void)p;(void)n;(void)s;}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t n) {(void)p;if(n==TP_PEN_Pin)pen_reads++;return GPIO_PIN_SET;}
static const int target[5][2]={{24,24},{455,24},{455,295},{24,295},{240,160}};
static void record_good(void) {
    Touch_StartCalibrate();
    for(unsigned i=0;i<5;i++)Touch_RecordCalPoint(i,3800-target[i][1]*10,200+target[i][0]*7);
}
int main(void) {
    record_good();CHECK(Touch_FinishCalibrate(),"swapped and inverted panel calibration succeeds");
    TouchCalib_t good=s_calib;
    for(unsigned i=0;i<5;i++) {
        float x=(200+target[i][0]*7)*s_calib.x_scale+s_calib.x_offset;
        float y=(3800-target[i][1]*10)*s_calib.y_scale+s_calib.y_offset;
        CHECK(fabsf(x-target[i][0])<2 && fabsf(y-target[i][1])<2,"all five transformed points accurate");
    }
    Touch_StartCalibrate();
    for(unsigned i=0;i<5;i++)Touch_RecordCalPoint(i,1000,1000);
    CHECK(!Touch_FinishCalibrate(),"degenerate samples rejected");
    Touch_CancelCalibrate();
    CHECK(Touch_GetCalStatus()==TOUCH_CAL_READY && s_calib.swap_xy==good.swap_xy && s_calib.x_scale==good.x_scale && s_calib.y_scale==good.y_scale,"failed recalibration/cancel preserves previous good transform");
    record_good();CHECK(Touch_FinishCalibrate(),"good calibration restored");
    Touch_StartCalibrate();Touch_RecordCalPoint(0,3560,368);
    CHECK(!Touch_FinishCalibrate(),"new calibration cannot reuse stale points from previous attempt");
    Touch_Init();
    s_last_pressed=true;s_last_x=100;s_last_y=200;
    TouchEvent_t release=Touch_Scan(0);
    CHECK(release.type==TOUCH_EVT_UP && release.x==100 && release.y==200,
          "first scan immediately delivers a pending release");
    unsigned reads=pen_reads;
    for(uint32_t tick=1;tick<TOUCH_SCAN_INTERVAL_MS;tick++)Touch_Scan(tick);
    CHECK(pen_reads==reads,"faster main-loop polling does not oversample touch");
    Touch_Scan(TOUCH_SCAN_INTERVAL_MS);
    CHECK(pen_reads==reads+1,"touch scans resume at the configured interval");
    printf("Touch calibration regression failures: %u\n",failures);
    return failures?1:0;
}
