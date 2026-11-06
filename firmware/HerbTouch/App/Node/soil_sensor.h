#ifndef HERB_SOIL_SENSOR_H
#define HERB_SOIL_SENSOR_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* SN-3002-TR-*-N01 Ver2.0, section5.3. N/P/K are device reference/
   holding-register values, not independent laboratory measurements. */
typedef struct {
    int16_t temperature_x10;
    uint16_t moisture_x10,ec_us_cm,ph_x10,n_mg_kg,p_mg_kg,k_mg_kg;
} SoilReading;
void Soil_Request(uint8_t slave,uint8_t frame[8]);
bool Soil_Valid(const SoilReading *reading);
bool Soil_Decode(uint8_t slave,const uint8_t *frame,size_t size,SoilReading *reading);
#endif
