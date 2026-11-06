#ifndef HERB_AIR780_H
#define HERB_AIR780_H
#include "at_stream.h"
typedef bool (*ModemSend)(const uint8_t *bytes,unsigned size);
typedef struct {
    unsigned step,failures,reconnects,published,stored;
    uint32_t deadline,next_clock_ms,wall_seconds,wall_tick;
    bool enabled,online,waiting,ready,clock_valid,ca_ready,acknowledged,command_ok;
    char last_line[96],fault[64],sample_id[128],payload[1024];
    unsigned payload_size;
    bool binding_mode;unsigned binding_result;
    ATStream stream;ModemSend send;
    unsigned binding_target;
} Air780;
void Air780_Init(Air780 *m,ModemSend send);
void Air780_Start(Air780 *m,uint32_t now);
void Air780_Feed(Air780 *m,const uint8_t *bytes,unsigned size);
void Air780_Poll(Air780 *m,uint32_t now);
void Air780_Fault(Air780 *m,const char *reason,uint32_t now);
bool Air780_Offer(Air780 *m,const char *id,const char *json);
bool Air780_OfferBinding(Air780 *m,const char *request_id,const char *json);
bool Air780_OfferNodeRequest(Air780 *m,unsigned target,const char *request_id,const char *json);
void Air780_Release(Air780 *m);
bool Air780_CommitReady(const Air780 *m);
bool Air780_Time(const Air780 *m,uint32_t now,uint32_t *beijing_seconds);
#endif
