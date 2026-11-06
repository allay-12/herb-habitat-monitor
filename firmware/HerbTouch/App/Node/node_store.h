#ifndef HERB_NODE_STORE_H
#define HERB_NODE_STORE_H
#include "air_sensor.h"
#include "soil_sensor.h"
#include "node_config.h"
typedef struct {
    char id[128],json[NODE_PAYLOAD_MAX],path[48];
    AirReading reading;uint32_t stamp;
    SoilReading soil;bool air_valid,soil_valid;
} NodeRecord;
bool NodeRecord_Build(NodeRecord *record,const char *id,const AirReading *reading,uint32_t stamp);
bool NodeRecord_BuildSensors(NodeRecord *record,const char *id,const AirReading *air,const SoilReading *soil,uint32_t stamp);
bool NodeStore_Save(NodeRecord *record,uint32_t key);
/* Explicit diagnostic path: marked test JSON, separate archive, no live CSV. */
bool NodeStore_SaveUploadTest(NodeRecord *record,uint32_t key);
const char *NodeStore_UploadTestArchive(void);
bool NodeStore_Load(NodeRecord *record);
bool NodeStore_QueueDrained(void);
bool NodeStore_Commit(const NodeRecord *record);
uint32_t NodeStore_Error(void);
uint32_t NodeStore_Corrupt(void);
#endif
