#ifndef HERB_HISTORY_FS_H
#define HERB_HISTORY_FS_H
#include <stdint.h>
#include <stdbool.h>
typedef enum {HFS_OK,HFS_END,HFS_MISSING,HFS_NO_CARD,HFS_ERROR,HFS_SKIP,HFS_DRAIN} HistoryFSResult;
void HistoryFS_SelectMetric(uint8_t metric);
HistoryFSResult HistoryFS_Mount(void);
HistoryFSResult HistoryFS_ListBegin(void);
HistoryFSResult HistoryFS_ListNext(uint32_t *date);
void HistoryFS_ListEnd(void);
HistoryFSResult HistoryFS_OpenDate(uint32_t date);
HistoryFSResult HistoryFS_ReadLine(char *line,uint32_t capacity);
void HistoryFS_Close(void);
uint32_t HistoryFS_Tick(void);
uint32_t HistoryFS_LastResult(void);
uint32_t HistoryFS_Generation(void);
void HistoryFS_SetTestMode(bool enabled);
#endif
