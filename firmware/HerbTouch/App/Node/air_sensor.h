#ifndef HERB_AIR_SENSOR_H
#define HERB_AIR_SENSOR_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { int16_t temperature_x10; uint16_t humidity_x10; uint32_t lux; } AirReading;
uint16_t Air_CRC(const uint8_t *data,size_t length);
void Air_Request(uint8_t slave,uint8_t frame[8]);
bool Air_Decode(uint8_t slave,const uint8_t *frame,size_t length,AirReading *out);
#endif
