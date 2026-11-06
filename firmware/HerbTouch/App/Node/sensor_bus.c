#include "sensor_bus.h"
#include "node_config.h"
#include <string.h>
static bool due(uint32_t now,uint32_t deadline){return (int32_t)(now-deadline)>=0;}
void SensorBus_Init(SensorBus *b,SensorBusSend send,uint32_t now)
{
    memset(b,0,sizeof(*b));b->send=send;
    b->next_due[0]=b->next_due[1]=now+NODE_SENSOR_WARMUP_MS;b->quiet_until=now;
}
void SensorBus_Feed(SensorBus *b,const uint8_t *p,unsigned n,uint32_t now)
{
    if(!b->waiting)return;
    b->last_byte=now;
    while(n--){if(b->size<sizeof(b->frame))b->frame[b->size++]=*p;else b->overflow=true;p++;}
}
static void finish(SensorBus *b,uint32_t now,bool ok)
{
    unsigned active=b->active;
    if(ok){b->frames[active]++;b->valid[active]=true;b->last_good[active]=now;b->exception[active]=0;}
    else b->errors[active]++;
    b->waiting=false;b->size=0;b->overflow=false;
    b->next_due[active]=now+NODE_SENSOR_POLL_MS;b->next=(uint8_t)(active^1u);
    b->quiet_until=now+NODE_SENSOR_GAP_MS;
}
static bool decode(SensorBus *b)
{
    uint8_t address=b->active==SENSOR_AIR?NODE_AIR_ADDRESS:NODE_SOIL_ADDRESS;
    unsigned n=b->active==SENSOR_AIR?21u:19u;
    if(b->overflow)return false;
    /* Ignore adapter TX echoes and unrelated bytes, but require a complete,
       address/length/CRC-validated response from the requested slave. */
    for(unsigned i=0;i+n<=b->size;i++) {
        if(b->active==SENSOR_AIR) {
            if(Air_Decode(address,b->frame+i,n,&b->air))return true;
        } else if(Soil_Decode(address,b->frame+i,n,&b->soil))return true;
    }
    for(unsigned i=0;i+5<=b->size;i++) {
        const uint8_t *p=b->frame+i;
        if(p[0]==address && p[1]==0x83 && !Air_CRC(p,5)) {
            b->exceptions[b->active]++;b->exception[b->active]=p[2];break;
        }
    }
    return false;
}
void SensorBus_ResetLink(SensorBus *b,uint32_t now)
{
    if(b->waiting)finish(b,now,false);
    b->quiet_until=now+NODE_SENSOR_GAP_MS;
}
void SensorBus_Poll(SensorBus *b,uint32_t now)
{
    for(unsigned i=0;i<2;i++)if(b->valid[i] && now-b->last_good[i]>NODE_SENSOR_STALE_MS)b->valid[i]=false;
    if(b->waiting) {
        if(due(now,b->deadline)){b->timeouts[b->active]++;finish(b,now,false);}
        else if(b->size && now-b->last_byte>=12)finish(b,now,decode(b));
        else return;
    }
    if(!due(now,b->quiet_until))return;
    for(unsigned k=0;k<2;k++) {
        unsigned index=(b->next+k)%2;
        if(!due(now,b->next_due[index]))continue;
        uint8_t request[8];
        if(index==SENSOR_AIR)Air_Request(NODE_AIR_ADDRESS,request);else Soil_Request(NODE_SOIL_ADDRESS,request);
        if(b->send && b->send(request,sizeof(request))) {
            b->active=(uint8_t)index;b->waiting=true;b->size=0;b->overflow=false;
            b->deadline=now+NODE_SENSOR_TIMEOUT_MS;b->last_byte=now;
        }
        return;
    }
}
