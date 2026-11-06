#ifndef HERB_NODE_APP_H
#define HERB_NODE_APP_H
#include <stdint.h>
/* Readable in debugger. Commands:1=both sensors on,2=modem on,3=all on,
   4=set local UTC+8 clock from time_to_set (seconds since2000).
   5=service-only offline save, 6=query occupancy, 7=request online move.
   Commands never touch PWRKEY or cut a running modem's supply. */
typedef struct {
    volatile uint32_t command;
    uint32_t sensor_frames,sensor_errors,uart_errors,samples,queue_errors,history_errors,stored;
    uint32_t sensor_enabled,modem_enabled,online,clock_valid,sd_error;
    char modem_last_line[96],modem_fault[64];
    uint32_t air_frames,soil_frames,air_errors,soil_errors;
    uint32_t time_to_set,local_seconds,clock_source,time_set_errors;
    uint32_t config_node_request,config_result,node_number,binding_state;
} NodeReport;
extern volatile NodeReport g_node;
/* Main-loop cycle state. Counts include each fresh valid reply after warm-up.
   phase=1 collects, phase=0 rests. A pending mean waits for time/SD readiness. */
typedef struct {
    uint32_t phase,started_ms,next_start_ms,completed,air_count,soil_count;
    uint32_t average_pending;
} NodeSensorCycleReport;
extern volatile NodeSensorCycleReport g_sensor_cycle;
void Node_Init(void);
void Node_Poll(uint32_t now);
#endif
