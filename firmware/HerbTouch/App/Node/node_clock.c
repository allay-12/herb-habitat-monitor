#include "node_clock.h"
void NodeClock_Init(NodeClock *c,uint32_t now){c->seconds=c->millis=c->source=0;c->tick=now;}
bool NodeClock_Set(NodeClock *c,uint32_t seconds,uint32_t now,uint32_t source)
{
    if(seconds<757382400u || seconds>3155759999u || (source!=NODE_CLOCK_MANUAL && source!=NODE_CLOCK_NETWORK))return false;
    c->seconds=seconds;c->tick=now;c->millis=0;c->source=source;return true;
}
void NodeClock_Poll(NodeClock *c,uint32_t now)
{
    uint64_t elapsed=(uint64_t)(now-c->tick)+c->millis;c->tick=now;
    if(!c->source)return;
    uint64_t next=c->seconds+elapsed/1000;
    if(next>3155759999u){c->source=0;return;}
    c->seconds=(uint32_t)next;c->millis=(uint32_t)(elapsed%1000);
}
bool NodeClock_Get(const NodeClock *c,uint32_t *seconds)
{if(!seconds || !c->source)return false;*seconds=c->seconds;return true;}
