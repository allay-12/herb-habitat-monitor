#ifndef __APP_UI_H
#define __APP_UI_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UI_PAGE_OVERVIEW = 0,
    UI_PAGE_SOIL,
    UI_PAGE_HISTORY,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT
} UIPage_t;

void UI_Init(void);
void UI_Process(uint32_t now_ms);
void UI_ShowDataPage(UIPage_t page);

#endif /* __APP_UI_H */
