#ifndef HERB_NODE_CLOCK_H
#define HERB_NODE_CLOCK_H
#include <stdint.h>
#include <stdbool.h>
enum {NODE_CLOCK_UNKNOWN=0,NODE_CLOCK_MANUAL=1,NODE_CLOCK_NETWORK=2};
typedef struct {uint32_t seconds,tick,millis,source;} NodeClock;
void NodeClock_Init(NodeClock *clock,uint32_t now);
bool NodeClock_Set(NodeClock *clock,uint32_t seconds,uint32_t now,uint32_t source);
void NodeClock_Poll(NodeClock *clock,uint32_t now);
bool NodeClock_Get(const NodeClock *clock,uint32_t *seconds);
#endif
