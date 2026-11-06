#include "sensor_bus.h"
#include "node_clock.h"
#include "node_config.h"
#include "history.h"
#include <stdio.h>
#include <string.h>
static unsigned failures,sends;static uint8_t sent[8];static bool busy;
#define CHECK(c,msg) do{if(!(c)){printf("FAIL: %s (line%d)\n",msg,__LINE__);failures++;}}while(0)
static bool send(const uint8_t *p,unsigned n){if(busy)return false;CHECK(n==8,"bounded read request");memcpy(sent,p,8);sends++;return true;}
static void crc(uint8_t *p,unsigned n){uint16_t c=Air_CRC(p,n-2);p[n-2]=(uint8_t)c;p[n-1]=(uint8_t)(c>>8);}
static void soil_frame(uint8_t *p)
{
    const uint8_t frame[]={1,3,14,0x02,0x92,0xff,0x9b,0x03,0xe8,0,56,0,30,0,20,0,50,0,0};memcpy(p,frame,19);crc(p,19);
}
int main(void)
{
    uint8_t f[19],req[8];SoilReading r;
    soil_frame(f);Soil_Request(1,req);
    const uint8_t expected[]={1,3,0,0,0,7,4,8};
    CHECK(!memcmp(req,expected,8)&&!Air_CRC(req,8),"manual soil addr1 registers0000..0006 read-only request");
    CHECK(Soil_Decode(1,f,19,&r)&&r.temperature_x10==-101&&r.moisture_x10==658&&r.ec_us_cm==1000&&r.ph_x10==56&&r.n_mg_kg==30&&r.p_mg_kg==20&&r.k_mg_kg==50,"all seven manual register positions and scales");
    CHECK(!Soil_Decode(2,f,19,&r)&&!Soil_Decode(1,f,18,&r)&&!Soil_Decode(1,0,19,&r),"wrong address, short and null responses rejected");
    f[7]^=1;CHECK(!Soil_Decode(1,f,19,&r),"bad CRC rejected");f[7]^=1;
    f[9]=1;crc(f,19);CHECK(!Soil_Decode(1,f,19,&r),"invalid PH not accepted as live data");soil_frame(f);
    for(unsigned split=0;split<=19;split++) {
        SensorBus bus;sends=0;SensorBus_Init(&bus,send,0);SensorBus_Poll(&bus,NODE_SENSOR_WARMUP_MS-1);
        CHECK(!sends,"nonblocking power warm-up");SensorBus_Poll(&bus,NODE_SENSOR_WARMUP_MS);
        CHECK(sends==1&&sent[0]==2,"air address2 is requested first");
        SensorBus_Poll(&bus,NODE_SENSOR_WARMUP_MS+NODE_SENSOR_TIMEOUT_MS);
        CHECK(bus.timeouts[0]==1&&!bus.waiting,"missing air times out without blocking soil");
        uint32_t t=NODE_SENSOR_WARMUP_MS+NODE_SENSOR_TIMEOUT_MS+NODE_SENSOR_GAP_MS;
        SensorBus_Poll(&bus,t);CHECK(sends==2&&sent[0]==1&&sent[5]==7,"next shared-bus request is soil1");
        SensorBus_Feed(&bus,f,split,t+10);SensorBus_Feed(&bus,f+split,19-split,t+11);
        SensorBus_Poll(&bus,t+20);CHECK(!bus.valid[1],"no early frame processing before RTU silence");
        SensorBus_Poll(&bus,t+23);
        CHECK(bus.valid[1]&&!bus.valid[0]&&bus.frames[1]==1&&bus.soil.n_mg_kg==30,"all response chunk boundaries preserve independent validity");
        SensorBus_Poll(&bus,t+23+NODE_SENSOR_STALE_MS+1);CHECK(!bus.valid[1],"stale soil becomes invalid independently");
    }
    SensorBus b;SensorBus_Init(&b,send,0);SensorBus_Poll(&b,NODE_SENSOR_WARMUP_MS);
    uint8_t air[21]={2,3,16,2,0x92,0xff,0x9b,0,0,0,0,0,0,0,0,0,1,0x86,0xa0,0,0};crc(air,21);
    SensorBus_Feed(&b,sent,8,30010);SensorBus_Feed(&b,air,21,30010);SensorBus_Poll(&b,30022);
    CHECK(b.valid[0]&&b.air.lux==100000,"adapter request echo does not corrupt reply");
    SensorBus_Poll(&b,30222);uint8_t exception[]={1,0x83,2,0,0};crc(exception,5);
    SensorBus_Feed(&b,exception,5,30230);SensorBus_Poll(&b,30242);
    CHECK(b.exceptions[1]==1&&b.exception[1]==2&&!b.valid[1],"Modbus exception is diagnosed, not a measurement");
    SensorBus_Init(&b,send,0xfffffff0u);SensorBus_Poll(&b,0xfffffff0u+NODE_SENSOR_WARMUP_MS);
    CHECK(b.waiting&&b.active==0,"poll deadlines work across millisecond tick wrap");
    SensorBus_ResetLink(&b,40000);CHECK(!b.waiting&&b.errors[0]==1,"UART recovery discards incomplete transaction");
    busy=true;SensorBus_Init(&b,send,0);SensorBus_Poll(&b,30000);CHECK(!b.waiting,"busy UART does not create phantom request");busy=false;
    NodeClock clock;NodeClock_Init(&clock,0);uint32_t stamp,current;
    CHECK(!NodeClock_Get(&clock,&current),"no invented wall time at boot");
    HistoryDate date={2026,11,2,12,0,0};HistoryDate_ToSeconds(&date,&stamp);
    CHECK(NodeClock_Set(&clock,stamp,0xfffffff0u,NODE_CLOCK_MANUAL),"explicit local UTC+8 clock set");
    NodeClock_Poll(&clock,984);CHECK(NodeClock_Get(&clock,&current)&&current==stamp+1,"local clock advances across tick wrap without modem");
    CHECK(!NodeClock_Set(&clock,0,1,NODE_CLOCK_MANUAL)&&NodeClock_Get(&clock,&current)&&current==stamp+1,"invalid time does not replace known clock");
    printf("Dual sensor bus/protocol/clock regression failures: %u\n",failures);return failures?1:0;
}
