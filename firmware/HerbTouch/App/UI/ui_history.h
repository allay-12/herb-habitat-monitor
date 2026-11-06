#ifndef HERB_UI_HISTORY_H
#define HERB_UI_HISTORY_H
#include "bsp_touch.h"
void HistoryUI_Init(void);
void HistoryUI_Enter(void);
void HistoryUI_SelectMetric(uint8_t metric);
void HistoryUI_Leave(void);
void HistoryUI_Process(uint32_t now_ms);
bool HistoryUI_Touch(const TouchEvent_t *event);
#endif
