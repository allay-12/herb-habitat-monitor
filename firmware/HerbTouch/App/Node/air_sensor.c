#include "air_sensor.h"
/* SN-300BYH-M Ver2.0, section4.3: decimal registers500..507. */
uint16_t Air_CRC(const uint8_t *p,size_t n)
{
    uint16_t crc=0xffff;
    while(n--){crc^=*p++;for(unsigned i=0;i<8;i++)crc=(crc>>1)^((crc&1)?0xa001:0);}
    return crc;
}
void Air_Request(uint8_t slave,uint8_t f[8])
{
    f[0]=slave;f[1]=3;f[2]=1;f[3]=0xf4;f[4]=0;f[5]=8;
    uint16_t crc=Air_CRC(f,6);f[6]=(uint8_t)crc;f[7]=(uint8_t)(crc>>8);
}
static uint16_t word(const uint8_t *p){return (uint16_t)((p[0]<<8)|p[1]);}
bool Air_Decode(uint8_t slave,const uint8_t *f,size_t n,AirReading *out)
{
    if(!f || !out || n!=21 || f[0]!=slave || f[1]!=3 || f[2]!=16 || Air_CRC(f,n))return false;
    AirReading r;r.humidity_x10=word(f+3);r.temperature_x10=(int16_t)word(f+5);
    r.lux=((uint32_t)word(f+15)<<16)|word(f+17);
    if(r.humidity_x10>1000 || r.temperature_x10 < -400 || r.temperature_x10>1200 || r.lux>200000)return false;
    *out=r;return true;
}
