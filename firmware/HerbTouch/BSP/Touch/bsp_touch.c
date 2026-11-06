#include "bsp_touch.h"
#include <stdlib.h>
#include <math.h>

#define TP_CS_LOW()     HAL_GPIO_WritePin(TP_CS_GPIO_Port, TP_CS_Pin, GPIO_PIN_RESET)
#define TP_CS_HIGH()    HAL_GPIO_WritePin(TP_CS_GPIO_Port, TP_CS_Pin, GPIO_PIN_SET)
#define TP_CLK_LOW()    HAL_GPIO_WritePin(TP_CLK_GPIO_Port, TP_CLK_Pin, GPIO_PIN_RESET)
#define TP_CLK_HIGH()   HAL_GPIO_WritePin(TP_CLK_GPIO_Port, TP_CLK_Pin, GPIO_PIN_SET)
#define TP_MOSI_LOW()   HAL_GPIO_WritePin(TP_MOSI_GPIO_Port, TP_MOSI_Pin, GPIO_PIN_RESET)
#define TP_MOSI_HIGH()  HAL_GPIO_WritePin(TP_MOSI_GPIO_Port, TP_MOSI_Pin, GPIO_PIN_SET)
#define TP_MISO_READ()  HAL_GPIO_ReadPin(TP_MISO_GPIO_Port, TP_MISO_Pin)
#define TP_PEN_READ()   HAL_GPIO_ReadPin(TP_PEN_GPIO_Port, TP_PEN_Pin)

#define CMD_READ_X      0xD0
#define CMD_READ_Y      0x90

static TouchCalib_t s_calib = {
    .x_scale  = 0.133f,
    .y_scale  = 0.089f,
    .x_offset = -20,
    .y_offset = -20,
    .swap_xy  = false,
    .status   = TOUCH_CAL_NONE
};

static uint16_t s_cal_raw_x[5];
static uint16_t s_cal_raw_y[5];
static uint8_t s_cal_mask = 0;
static TouchCalib_t s_good_calib;

static bool s_last_pressed = false;
static int16_t s_last_x = -1;
static int16_t s_last_y = -1;

static void Touch_DelayUs(uint32_t us)
{
    volatile uint32_t count = us * 40;
    while (count--)
    {
        __NOP();
    }
}

static void Touch_WriteByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        TP_CLK_LOW();
        if (byte & 0x80)
        {
            TP_MOSI_HIGH();
        }
        else
        {
            TP_MOSI_LOW();
        }
        byte <<= 1;
        Touch_DelayUs(1);
        TP_CLK_HIGH();
        Touch_DelayUs(1);
    }
}

static uint16_t Touch_ReadADC(uint8_t cmd)
{
    TP_CS_LOW();
    Touch_DelayUs(1);

    Touch_WriteByte(cmd);
    Touch_DelayUs(6); /* Wait for SAR conversion (~6us) */

    TP_CLK_LOW();
    Touch_DelayUs(1);
    TP_CLK_HIGH();
    Touch_DelayUs(1);
    TP_CLK_LOW();
    Touch_DelayUs(1);

    uint16_t num = 0;
    for (uint8_t i = 0; i < 16; i++)
    {
        num <<= 1;
        TP_CLK_LOW();
        Touch_DelayUs(1);
        TP_CLK_HIGH();
        Touch_DelayUs(1);
        if (TP_MISO_READ() == GPIO_PIN_SET)
        {
            num |= 1;
        }
    }
    num >>= 4;

    TP_CS_HIGH();
    return num;
}

void Touch_Init(void)
{
    TP_CS_HIGH();
    TP_CLK_LOW();
    TP_MOSI_LOW();

    /* Issue dummy conversions to place XPT2046 into PD=00 mode, activating PENIRQ */
    Touch_ReadADC(CMD_READ_X);
    Touch_ReadADC(CMD_READ_Y);
}

bool Touch_IsPressed(void)
{
    return (TP_PEN_READ() == GPIO_PIN_RESET);
}

static void BubbleSort(uint16_t arr[], uint8_t n)
{
    for (uint8_t i = 0; i < n - 1; i++)
    {
        for (uint8_t j = 0; j < n - i - 1; j++)
        {
            if (arr[j] > arr[j + 1])
            {
                uint16_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

bool Touch_ReadRaw(uint16_t *x, uint16_t *y)
{
    if (!Touch_IsPressed()) return false;

    uint16_t x_buf[5];
    uint16_t y_buf[5];

    for (uint8_t i = 0; i < 5; i++)
    {
        x_buf[i] = Touch_ReadADC(CMD_READ_X);
        y_buf[i] = Touch_ReadADC(CMD_READ_Y);
        Touch_DelayUs(5);
    }

    /* Check if still pressed */
    if (!Touch_IsPressed()) return false;

    BubbleSort(x_buf, 5);
    BubbleSort(y_buf, 5);

    /* Discard min and max, average middle 3 */
    *x = (x_buf[1] + x_buf[2] + x_buf[3]) / 3;
    *y = (y_buf[1] + y_buf[2] + y_buf[3]) / 3;

    return true;
}

bool Touch_ReadPoint(int16_t *x, int16_t *y)
{
    uint16_t raw_x = 0, raw_y = 0;
    if (!Touch_ReadRaw(&raw_x, &raw_y)) return false;

    float px = (float)raw_x;
    float py = (float)raw_y;

    if (s_calib.swap_xy)
    {
        float temp = px;
        px = py;
        py = temp;
    }

    int16_t calc_x = (int16_t)(px * s_calib.x_scale + s_calib.x_offset);
    int16_t calc_y = (int16_t)(py * s_calib.y_scale + s_calib.y_offset);

    if (calc_x < 0) calc_x = 0;
    if (calc_x >= 480) calc_x = 479;
    if (calc_y < 0) calc_y = 0;
    if (calc_y >= 320) calc_y = 319;

    *x = calc_x;
    *y = calc_y;
    return true;
}

TouchEvent_t Touch_Scan(uint32_t now_ms)
{
    (void)now_ms;
    TouchEvent_t evt = { .type = TOUCH_EVT_NONE, .x = -1, .y = -1 };
    bool pressed = Touch_IsPressed();

    if (pressed)
    {
        int16_t cur_x = 0, cur_y = 0;
        if (Touch_ReadPoint(&cur_x, &cur_y))
        {
            s_last_x = cur_x;
            s_last_y = cur_y;

            if (!s_last_pressed)
            {
                s_last_pressed = true;
                evt.type = TOUCH_EVT_DOWN;
                evt.x = cur_x;
                evt.y = cur_y;
            }
            else
            {
                evt.type = TOUCH_EVT_MOVE;
                evt.x = cur_x;
                evt.y = cur_y;
            }
        }
    }
    else
    {
        if (s_last_pressed)
        {
            s_last_pressed = false;
            evt.type = TOUCH_EVT_UP;
            evt.x = s_last_x;
            evt.y = s_last_y;
        }
    }

    return evt;
}

TouchCalStatus_t Touch_GetCalStatus(void)
{
    return s_calib.status;
}

void Touch_StartCalibrate(void)
{
    s_cal_mask = 0;
    s_calib.status = TOUCH_CALIBRATING;
}

void Touch_CancelCalibrate(void)
{
    if (s_good_calib.status == TOUCH_CAL_READY)
    {
        s_calib = s_good_calib;
    }
    else
    {
        s_calib.status = TOUCH_CAL_NONE;
    }
}

bool Touch_RecordCalPoint(uint8_t step, uint16_t raw_x, uint16_t raw_y)
{
    if (step >= 5 || raw_x > 4095 || raw_y > 4095 || s_calib.status != TOUCH_CALIBRATING) return false;
    s_cal_raw_x[step] = raw_x;
    s_cal_raw_y[step] = raw_y;
    s_cal_mask |= (uint8_t)(1u << step);
    return true;
}

bool Touch_FinishCalibrate(void)
{
    TouchCalib_t candidate = s_calib;
    static const int16_t targets[5][2] = {
        {24,24},{455,24},{455,295},{24,295},{240,160}
    };
    if (s_calib.status != TOUCH_CALIBRATING || s_cal_mask != 0x1f)
    {
        s_calib.status = TOUCH_CAL_FAILED;
        return false;
    }
    /* 5 targets:
       0: (24, 24)
       1: (455, 24)
       2: (455, 295)
       3: (24, 295)
       4: (240, 160) - verification
    */
    int32_t dx01 = abs((int32_t)s_cal_raw_x[1] - (int32_t)s_cal_raw_x[0]);
    int32_t dy01 = abs((int32_t)s_cal_raw_y[1] - (int32_t)s_cal_raw_y[0]);

    bool swap = (dy01 > dx01);
    candidate.swap_xy = swap;

    float r0x = swap ? s_cal_raw_y[0] : s_cal_raw_x[0];
    float r1x = swap ? s_cal_raw_y[1] : s_cal_raw_x[1];
    float r2x = swap ? s_cal_raw_y[2] : s_cal_raw_x[2];
    float r3x = swap ? s_cal_raw_y[3] : s_cal_raw_x[3];

    float r0y = swap ? s_cal_raw_x[0] : s_cal_raw_y[0];
    float r1y = swap ? s_cal_raw_x[1] : s_cal_raw_y[1];
    float r2y = swap ? s_cal_raw_x[2] : s_cal_raw_y[2];
    float r3y = swap ? s_cal_raw_x[3] : s_cal_raw_y[3];

    float raw_x_left  = (r0x + r3x) / 2.0f;
    float raw_x_right = (r1x + r2x) / 2.0f;
    float raw_y_top   = (r0y + r1y) / 2.0f;
    float raw_y_bot   = (r2y + r3y) / 2.0f;

    if (fabs(raw_x_right - raw_x_left) < 100 || fabs(raw_y_bot - raw_y_top) < 100)
    {
        s_calib.status = TOUCH_CAL_FAILED;
        return false;
    }

    candidate.x_scale = (455.0f - 24.0f) / (raw_x_right - raw_x_left);
    candidate.x_offset = 24.0f - (candidate.x_scale * raw_x_left);

    candidate.y_scale = (295.0f - 24.0f) / (raw_y_bot - raw_y_top);
    candidate.y_offset = 24.0f - (candidate.y_scale * raw_y_top);

    /* Commit only after all corners and the independent center check pass.
       A failed retry must not replace a previously usable coordinate map. */
    for (uint8_t i = 0; i < 5; i++)
    {
        float rx = swap ? s_cal_raw_y[i] : s_cal_raw_x[i];
        float ry = swap ? s_cal_raw_x[i] : s_cal_raw_y[i];
        if (fabsf(rx * candidate.x_scale + candidate.x_offset - targets[i][0]) > 10.0f ||
            fabsf(ry * candidate.y_scale + candidate.y_offset - targets[i][1]) > 10.0f)
        {
            s_calib.status = TOUCH_CAL_FAILED;
            return false;
        }
    }
    candidate.status = TOUCH_CAL_READY;
    s_calib = candidate;
    s_good_calib = candidate;
    return true;
}
