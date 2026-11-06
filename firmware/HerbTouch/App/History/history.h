#ifndef HERB_HISTORY_H
#define HERB_HISTORY_H
#include <stdint.h>
#include <stdbool.h>

#define HISTORY_MAX_POINTS 96
#define HISTORY_METRICS 10
#define HISTORY_LINE_SIZE 384
#define HISTORY_CSV_HEADER "timestamp,air_temperature_c,air_humidity_pct,illuminance_lux,soil_temperature_c,soil_moisture_pct,soil_ec_us_cm,soil_ph,soil_n_mg_kg,soil_p_mg_kg,soil_k_mg_kg"

typedef struct {uint16_t year;uint8_t month,day,hour,minute,second;} HistoryDate;
typedef enum {HISTORY_HOUR,HISTORY_DAY,HISTORY_MONTH} HistoryUnit;
typedef enum {
    HISTORY_IDLE,HISTORY_FINDING,HISTORY_READING,HISTORY_READY,
    HISTORY_NO_CARD,HISTORY_NO_DATA,HISTORY_FORMAT_ERROR,HISTORY_IO_ERROR
} HistoryStatus;
typedef struct {double sum;float min,max;uint32_t count,first_stamp,last_stamp;} HistoryPoint;
typedef struct {
    HistoryStatus status;
    HistoryUnit unit;
    uint8_t metric,points;
    uint32_t start,end,stride,latest,rows,bad_rows,bad_files,files,resolution_seconds;
    uint32_t boundary[HISTORY_MAX_POINTS+1];
    HistoryPoint point[HISTORY_MAX_POINTS];
    bool latest_valid;
} HistorySeries;

bool HistoryDate_ToSeconds(const HistoryDate *date,uint32_t *seconds);
void HistoryDate_FromSeconds(uint32_t seconds,HistoryDate *date);
bool HistoryDate_Parse(const char *text,uint32_t *seconds);
uint8_t HistoryDate_Days(uint16_t year,uint8_t month);
uint32_t HistoryDate_Floor(uint32_t value,HistoryUnit unit);
bool HistoryDate_Shift(uint32_t value,HistoryUnit unit,int32_t amount,uint32_t *result);
void HistoryDate_Format(uint32_t value,char *text,uint32_t capacity);

/* All file access is read-only and advanced incrementally by Process(). */
void History_Init(void);
void History_Refresh(uint8_t metric,HistoryUnit unit);
bool History_Query(uint8_t metric,HistoryUnit unit,uint32_t start,uint32_t end);
void History_Default(uint8_t metric,HistoryUnit unit);
void History_Process(void);
void History_Cancel(void);
/* Called only after real CSV data has been synced/closed, never for a test. */
void History_NotifyWrite(uint32_t stamp);
uint32_t History_DataRevision(void);
const HistorySeries *History_Get(void);
const char *History_MetricName(uint8_t metric);
const char *History_MetricUnit(uint8_t metric);
bool History_ParseRow(char *line,uint32_t *stamp,float values[HISTORY_METRICS],uint16_t *valid);
#endif
