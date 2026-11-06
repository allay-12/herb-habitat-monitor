#ifndef HERB_SENSOR_BUS_H
#define HERB_SENSOR_BUS_H
#include "air_sensor.h"
#include "soil_sensor.h"
enum {SENSOR_AIR=0,SENSOR_SOIL=1,SENSOR_COUNT=2};
typedef bool (*SensorBusSend)(const uint8_t *data,unsigned size);
typedef struct {
    SensorBusSend send;
    AirReading air;SoilReading soil;
    bool valid[2],waiting,overflow;
    uint8_t active,next,frame[64];unsigned size;
    uint32_t next_due[2],last_good[2],frames[2],errors[2],timeouts[2],exceptions[2];
    uint8_t exception[2];
    uint32_t last_byte,deadline,quiet_until;
} SensorBus;
/* A single request is active on USART3 at a time. Neither driver writes config. */
void SensorBus_Init(SensorBus *bus,SensorBusSend send,uint32_t now);
void SensorBus_Feed(SensorBus *bus,const uint8_t *bytes,unsigned size,uint32_t now);
void SensorBus_Poll(SensorBus *bus,uint32_t now);
void SensorBus_ResetLink(SensorBus *bus,uint32_t now);
#endif
