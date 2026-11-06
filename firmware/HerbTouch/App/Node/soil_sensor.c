#include "soil_sensor.h"
#include "air_sensor.h"
static uint16_t word(const uint8_t *p){return (uint16_t)((p[0]<<8)|p[1]);}
void Soil_Request(uint8_t slave,uint8_t frame[8])
{
    frame[0]=slave;frame[1]=3;frame[2]=frame[3]=frame[4]=0;frame[5]=7;
    uint16_t crc=Air_CRC(frame,6);frame[6]=(uint8_t)crc;frame[7]=(uint8_t)(crc>>8);
}
bool Soil_Valid(const SoilReading *r)
{
    return r && r->temperature_x10>=-400 && r->temperature_x10<=800 &&
        r->moisture_x10<=1000 && r->ec_us_cm<=20000 && r->ph_x10>=30 && r->ph_x10<=90 &&
        r->n_mg_kg<=2999 && r->p_mg_kg<=2999 && r->k_mg_kg<=2999;
}
bool Soil_Decode(uint8_t slave,const uint8_t *f,size_t n,SoilReading *out)
{
    if(!f || !out || slave<1 || slave>254 || n!=19 || f[0]!=slave || f[1]!=3 || f[2]!=14 || Air_CRC(f,n))return false;
    SoilReading r;
    r.moisture_x10=word(f+3);r.temperature_x10=(int16_t)word(f+5);r.ec_us_cm=word(f+7);
    r.ph_x10=word(f+9);r.n_mg_kg=word(f+11);r.p_mg_kg=word(f+13);r.k_mg_kg=word(f+15);
    if(!Soil_Valid(&r))return false;
    *out=r;return true;
}
