#ifndef __BSP_TOUCH_H
#define __BSP_TOUCH_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TOUCH_EVT_NONE = 0,
    TOUCH_EVT_DOWN,
    TOUCH_EVT_MOVE,
    TOUCH_EVT_UP
} TouchEventType_t;

typedef struct {
    TouchEventType_t type;
    int16_t x;
    int16_t y;
} TouchEvent_t;

typedef enum {
    TOUCH_CAL_NONE = 0,
    TOUCH_CALIBRATING,
    TOUCH_CAL_READY,
    TOUCH_CAL_FAILED
} TouchCalStatus_t;

typedef struct {
    float   x_scale;
    float   y_scale;
    int16_t x_offset;
    int16_t y_offset;
    bool    swap_xy;
    TouchCalStatus_t status;
} TouchCalib_t;

void              Touch_Init(void);
bool              Touch_IsPressed(void);
bool              Touch_ReadRaw(uint16_t *x, uint16_t *y);
bool              Touch_ReadPoint(int16_t *x, int16_t *y);

/* Touch Event Polling (Call every ~10ms from main loop) */
TouchEvent_t      Touch_Scan(uint32_t now_ms);

/* Calibration */
TouchCalStatus_t  Touch_GetCalStatus(void);
void              Touch_StartCalibrate(void);
bool              Touch_RecordCalPoint(uint8_t step, uint16_t raw_x, uint16_t raw_y);
bool              Touch_FinishCalibrate(void);
void              Touch_CancelCalibrate(void);

#endif /* __BSP_TOUCH_H */
